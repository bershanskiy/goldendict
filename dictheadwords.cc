/* This file is (c) 2014 Abs62
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "dictheadwords.hh"
#include "gddebug.hh"
#include "mainwindow.hh"

#include <QRegExp>
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
#include <QtCore5Compat>
#endif
#include <QDir>
#include <QFileDialog>
#include <QTimer>
#include <QProgressDialog>

#include <QRegularExpression>
#include "wildcard.hh"
#include "gddebug.hh"
#include <QMessageBox>

#define AUTO_APPLY_LIMIT 150000

DictHeadwords::DictHeadwords( QWidget *parent, Config::Class & cfg_,
                              Dictionary::Class * dict_ ) :
  QDialog(parent)
, cfg( cfg_ )
, dict( dict_ )
, helpAction( this )
{
  ui.setupUi( this );

  bool fromMainWindow = parent->objectName() == "MainWindow";

  if( fromMainWindow )
    setAttribute( Qt::WA_DeleteOnClose, false );

  setWindowFlags( windowFlags() & ~Qt::WindowContextHelpButtonHint );

  if( cfg.headwordsDialog.headwordsDialogGeometry.size() > 0 )
    restoreGeometry( cfg.headwordsDialog.headwordsDialogGeometry );

  ui.searchModeCombo->addItem( tr( "Text" ), QRegExp::FixedString );
  ui.searchModeCombo->addItem( tr( "Wildcards" ), QRegExp::WildcardUnix );
  ui.searchModeCombo->addItem( tr( "RegExp" ), QRegExp::RegExp );
  ui.searchModeCombo->setCurrentIndex( cfg.headwordsDialog.searchMode );

  ui.exportButton->setAutoDefault( false );
  ui.OKButton->setAutoDefault( false);
  ui.applyButton->setAutoDefault( true );
  ui.applyButton->setDefault( true );

  ui.matchCase->setChecked( cfg.headwordsDialog.matchCase );

  model = new HeadwordListModel( this );
  proxy = new QSortFilterProxyModel( this );

  proxy->setSourceModel( model );

  proxy->setSortCaseSensitivity( Qt::CaseInsensitive );
  proxy->setSortLocaleAware( true );
  proxy->setDynamicSortFilter( false );

  ui.headersListView->setModel( proxy );
  ui.headersListView->setEditTriggers( QAbstractItemView::NoEditTriggers );

  // very important call, for performance reasons:
  ui.headersListView->setUniformItemSizes( true );

  delegate = new WordListItemDelegate( ui.headersListView->itemDelegate() );
  if( delegate )
    ui.headersListView->setItemDelegate( delegate );

  ui.autoApply->setChecked( cfg.headwordsDialog.autoApply );

  connect( this, &QDialog::finished, this, &DictHeadwords::savePos );

  if( !fromMainWindow )
  {
    ui.helpButton->hide();
    connect( this, &DictHeadwords::closeDialog, this, &QDialog::accept );
  }
  else
  {
    connect( ui.helpButton, &QAbstractButton::clicked, this, &DictHeadwords::helpRequested );

    helpAction.setShortcut( QKeySequence( "F1" ) );
    helpAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );

    connect( &helpAction, &QAction::triggered, this, &DictHeadwords::helpRequested );

    addAction( &helpAction );
  }

  connect( ui.OKButton, &QAbstractButton::clicked, this, &DictHeadwords::okButtonClicked );
  connect( ui.exportButton, &QAbstractButton::clicked, this, &DictHeadwords::exportButtonClicked );
  connect( ui.applyButton, &QAbstractButton::clicked, this, &DictHeadwords::filterChanged );

  connect( ui.autoApply, &QCheckBox::stateChanged, this, &DictHeadwords::autoApplyStateChanged );

  connect( ui.filterLine, &QLineEdit::textChanged, this, &DictHeadwords::filterChangedInternal );
  connect( ui.searchModeCombo, &QComboBox::currentIndexChanged, this, &DictHeadwords::filterChangedInternal );
  connect( ui.matchCase, &QCheckBox::stateChanged, this, &DictHeadwords::filterChangedInternal );

  connect( ui.headersListView, &QAbstractItemView::clicked, this, &DictHeadwords::itemClicked );

  connect( proxy, &QAbstractItemModel::dataChanged, this, &DictHeadwords::showHeadwordsNumber );

  ui.headersListView->installEventFilter( this );

  setup( dict_ );
}

DictHeadwords::~DictHeadwords()
{
  if( delegate )
    delegate->deleteLater();
}

void DictHeadwords::setup( Dictionary::Class *dict_ )
{
  QApplication::setOverrideCursor( Qt::WaitCursor );

  dict = dict_;

  setWindowTitle( QString::fromUtf8( dict->getName().c_str() ) );

  auto size = dict->getWordCount();
  model->setDict(dict);
  proxy->sort( 0 );
  filterChanged();

  if( size > AUTO_APPLY_LIMIT )
  {
    cfg.headwordsDialog.autoApply = ui.autoApply->isChecked();
    ui.autoApply->setChecked( false );
    ui.autoApply->setEnabled( false );
  }
  else
  {
    ui.autoApply->setEnabled( true );
    ui.autoApply->setChecked( cfg.headwordsDialog.autoApply );
  }

  ui.applyButton->setEnabled( !ui.autoApply->isChecked() );

  setWindowIcon( dict->getIcon() );

  dictId = QString( dict->getId().c_str() );

  QApplication::restoreOverrideCursor();
}

void DictHeadwords::savePos()
{
  cfg.headwordsDialog.searchMode = ui.searchModeCombo->currentIndex();
  cfg.headwordsDialog.matchCase = ui.matchCase->isChecked();

  if( model->totalCount() <= AUTO_APPLY_LIMIT )
    cfg.headwordsDialog.autoApply = ui.autoApply->isChecked();

  cfg.headwordsDialog.headwordsDialogGeometry = saveGeometry();
}

bool DictHeadwords::eventFilter( QObject * obj, QEvent * ev )
{
  if( obj == ui.headersListView && ev->type() == QEvent::KeyPress )
  {
    QKeyEvent * kev = static_cast< QKeyEvent * >( ev );
    if( kev->key() == Qt::Key_Return || kev->key() == Qt::Key_Enter )
    {
      itemClicked( ui.headersListView->currentIndex() );
      return true;
    }
  }
  return QDialog::eventFilter( obj, ev );
}

void DictHeadwords::okButtonClicked()
{
  savePos();
  closeDialog();
}

void DictHeadwords::reject()
{
  savePos();
  closeDialog();
}

void DictHeadwords::exportButtonClicked()
{
  saveHeadersToFile();
}

void DictHeadwords::filterChangedInternal()
{
  // emit signal in async manner, to avoid UI slowdown
  if( ui.autoApply->isChecked() )
    QTimer::singleShot( 100, this, &DictHeadwords::filterChanged );
}

void DictHeadwords::filterChanged()
{
  QRegExp::PatternSyntax syntax =
          QRegExp::PatternSyntax( ui.searchModeCombo->itemData(
                  ui.searchModeCombo->currentIndex()).toInt() );

  QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
  if( !ui.matchCase->isChecked() )
    options |= QRegularExpression::CaseInsensitiveOption;

  QString pattern;
  switch( syntax )
  {
  case QRegExp::FixedString:
    pattern = QRegularExpression::escape( ui.filterLine->text() );
    break;
  case QRegExp::WildcardUnix:
    pattern = wildcardsToRegexp( ui.filterLine->text() );
    break;
  default:
    pattern = ui.filterLine->text();
    break;
  }

  QRegularExpression regExp( pattern, options );

  if( !regExp.isValid() )
  {
    gdWarning( "Invalid regexp pattern: %s\n", pattern.toUtf8().data() );
    regExp.setPattern( QString::fromLatin1( "\1" ) );
  }

  QApplication::setOverrideCursor( Qt::WaitCursor );

  model->setFilter(regExp);

  proxy->setFilterRegularExpression( regExp );
  proxy->sort( 0 );

  QApplication::restoreOverrideCursor();

  showHeadwordsNumber();
}

void DictHeadwords::itemClicked( const QModelIndex & index )
{
  QVariant value = proxy->data( index, Qt::DisplayRole );
  if ( value.canConvert< QString >() )
  {
    QString headword = value.toString();
    emit headwordSelected( headword, dictId );
  }
}

void DictHeadwords::autoApplyStateChanged( int state )
{
  ui.applyButton->setEnabled( state == Qt::Unchecked );
}

void DictHeadwords::showHeadwordsNumber()
{
  ui.headersNumber->setText( tr( "Unique headwords total: %1, filtered: %2" )
                             .arg( QString::number( model->totalCount() ), QString::number( proxy->rowCount() ) ) );
}

void DictHeadwords::saveHeadersToFile()
{
  QString exportPath;
  if( cfg.headwordsDialog.headwordsExportPath.isEmpty() )
    exportPath = QDir::homePath();
  else
  {
    exportPath = QDir::fromNativeSeparators( cfg.headwordsDialog.headwordsExportPath );
    if( !QDir( exportPath ).exists() )
      exportPath = QDir::homePath();
  }

  QString fileName = QFileDialog::getSaveFileName( this, tr( "Save headwords to file" ),
                                                   exportPath,
                                                   tr( "Text files (*.txt);;All files (*.*)" ) );
  if( fileName.size() == 0)
      return;

  QFile file( fileName );

  if ( !file.open( QFile::WriteOnly | QIODevice::Text ) )
  {
    QMessageBox::critical( this, "GoldenDict", tr( "Can not open exported file" ) );
    return;
  }

  cfg.headwordsDialog.headwordsExportPath = QDir::toNativeSeparators(
                                              QFileInfo( fileName ).absoluteDir().absolutePath() );

  QSet< QString > allHeadwords;
  int headwordsNumber = model->totalCount();

  //headwordsNumber*2  ,  read + write
  QProgressDialog progress( tr( "Export headwords..." ), tr( "Cancel" ), 0, headwordsNumber*2, this );
  progress.setWindowModality( Qt::WindowModal );

  int totalCount=0;
  for( int i = 0; i < headwordsNumber && i < model->wordCount(); ++i )
  {
    if( progress.wasCanceled() )
      break;
    progress.setValue( totalCount++ );

    QVariant value = model->getRow( i );
    if( !value.canConvert< QString >() )
      continue;

    allHeadwords.insert( value.toString() );
  }

  // continue to write the remaining headword
  int nodeIndex  = model->getCurrentIndex();
  auto headwords = model->getRemainRows( nodeIndex );
  while( !headwords.isEmpty() )
  {
    if( progress.wasCanceled() )
      break;
    allHeadwords.unite(headwords);

    totalCount += headwords.size();
    progress.setValue( totalCount );

    headwords = model->getRemainRows( nodeIndex );
  }

  qDebug()<<model->getCurrentIndex();

  // Write UTF-8 BOM
  QByteArray line;
  line.append( 0xEF ).append( 0xBB ).append( 0xBF );
  file.write( line );

  QList< QString > sortedWords = allHeadwords.values();
  sortedWords.sort();

  // Write headwords
  for( auto const & word : sortedWords )
  {
    if( progress.wasCanceled() )
      break;
    progress.setValue( totalCount++ );

    line = word.toUtf8();

    line.replace( '\n', ' ' );
    line.replace( '\r', ' ' );

    line += "\n";

    if( file.write( line ) != line.size() )
      break;
  }

  file.close();

  if( progress.wasCanceled() )
  {
    QMessageBox::warning( this, "GoldenDict", tr( "Export process is interrupted" ) );
    gdWarning( "Headers export error: %s", file.errorString().toUtf8().data() );
  }
  else
  {
    //completed.
    progress.setValue(headwordsNumber*2);
    progress.hide();
    QMessageBox::information( this, "GoldenDict", tr( "Export finished" ) );
  }
}

void DictHeadwords::helpRequested()
{
  MainWindow * mainWindow = qobject_cast< MainWindow * >( parentWidget() );
  if( mainWindow )
    mainWindow->showGDHelpForID( "Dictionary headwords" );
}
