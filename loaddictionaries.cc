/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "loaddictionaries.hh"
#include "initializing.hh"
#include "bgl.hh"
#include "stardict.hh"
#include "lsa.hh"
#include "dsl.hh"
#include "mediawiki.hh"
#include "sounddir.hh"
#include "hunspell.hh"
#include "dictdfiles.hh"
#include "romaji.hh"
#include "russiantranslit.hh"
#include "german.hh"
#include "greektranslit.hh"
#include "belarusiantranslit.hh"
#include "website.hh"
#include "forvo.hh"
#include "programs.hh"
#include "voiceengines.hh"
#include "gddebug.hh"
#include "fsencoding.hh"
#include "xdxf.hh"
#include "sdict.hh"
#include "aard.hh"
#include "zipsounds.hh"
#include "mdx.hh"
#include "zim.hh"
#include "dictserver.hh"
#include "slob.hh"
#include "gls.hh"
#include "dict/lingualibre.h"

#ifndef NO_EPWING_SUPPORT
#include "epwing.hh"
#endif

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
#include "chinese.hh"
#endif

#include <QMessageBox>
#include <QDir>

#include <set>

using std::set;

using std::string;
using std::vector;

LoadDictionaries::LoadDictionaries( Config::Class const & cfg ):
  paths( cfg.paths ), soundDirs( cfg.soundDirs ), hunspell( cfg.hunspell ),
  transliteration( cfg.transliteration ),
  exceptionText( "Load did not finish" ), // Will be cleared upon success
  maxPictureWidth( cfg.maxPictureWidth ),
  maxHeadwordSize( cfg.maxHeadwordSize ),
  maxHeadwordToExpand( cfg.maxHeadwordsToExpand )
{
  // Populate name filters

  nameFilters << "*.bgl" << "*.ifo" << "*.lsa" << "*.dat"
              << "*.dsl" << "*.dsl.dz"  << "*.index" << "*.xdxf"
              << "*.xdxf.dz" << "*.dct" << "*.aar" << "*.zips"
              << "*.mdx" << "*.gls" << "*.gls.dz"
#ifdef MAKE_ZIM_SUPPORT
              << "*.zim" << "*.zimaa" << "*.slob"
#endif
#ifndef NO_EPWING_SUPPORT
              << "*catalogs"
#endif
;
}

void LoadDictionaries::run()
{
  try
  {
    for(const auto & path : paths)
      handlePath( path );

    // Make soundDirs
    {
      vector< sptr< Dictionary::Class > > soundDirDictionaries =
        SoundDir::makeDictionaries( soundDirs, FsEncoding::encode( Config::getIndexDir() ), *this );

      dictionaries.insert( dictionaries.end(), soundDirDictionaries.begin(),
                           soundDirDictionaries.end() );
    }

    // Make hunspells
    {
      vector< sptr< Dictionary::Class > > hunspellDictionaries =
        HunspellMorpho::makeDictionaries( hunspell );

      dictionaries.insert( dictionaries.end(), hunspellDictionaries.begin(),
                           hunspellDictionaries.end() );
    }

    exceptionText.clear();
  }
  catch( std::exception & e )
  {
    exceptionText = e.what();
  }
}

void LoadDictionaries::addDicts( const std::vector< sptr< Dictionary::Class > >& dicts ) {
  std::move(dicts.begin(), dicts.end(), std::back_inserter(dictionaries));
}

void LoadDictionaries::handlePath( Config::Path const & path )
{
  vector< string > allFiles;

  QDir dir( path.path );

  QFileInfoList entries = dir.entryInfoList( nameFilters, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot );

  for( QFileInfoList::const_iterator i = entries.constBegin();
       i != entries.constEnd(); ++i )
  {
    QString fullName = i->absoluteFilePath();

    if ( path.recursive && i->isDir() )
    {
      // Make sure the path doesn't look like with dsl resources
      if ( !fullName.endsWith( ".dsl.files", Qt::CaseInsensitive ) &&
           !fullName.endsWith( ".dsl.dz.files", Qt::CaseInsensitive ) )
        handlePath( Config::Path( fullName, true ) );
    }

    if ( !i->isDir() )
      allFiles.push_back( FsEncoding::encode( QDir::toNativeSeparators( fullName ) ) );
  }

  addDicts(Bgl::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
  addDicts(Stardict::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, maxHeadwordToExpand ) );
  addDicts(Lsa::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
  addDicts(Dsl::makeDictionaries( allFiles,FsEncoding::encode( Config::getIndexDir() ),*this,maxPictureWidth,maxHeadwordSize ) );
  addDicts(DictdFiles::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
  addDicts(Xdxf::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
  addDicts(Sdict::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
  addDicts(Aard::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, maxHeadwordToExpand ) );
  addDicts(ZipSounds::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
  addDicts(Mdx::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
  addDicts(Gls::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );

#ifdef MAKE_ZIM_SUPPORT
  addDicts(Zim::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, maxHeadwordToExpand ) );
  addDicts(Slob::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, maxHeadwordToExpand ) );
#endif
#ifndef NO_EPWING_SUPPORT
  addDicts( Epwing::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this ) );
#endif
}

void LoadDictionaries::indexingDictionary( string const & dictionaryName ) noexcept
{
  emit indexingDictionarySignal( QString::fromUtf8( dictionaryName.c_str() ) );
}


void loadDictionaries( QWidget * parent, bool showInitially,
                       Config::Class const & cfg,
                       std::vector< sptr< Dictionary::Class > > & dictionaries,
                       QNetworkAccessManager & dictNetMgr,
                       bool doDeferredInit_ )
{
  dictionaries.clear();

  ::Initializing init( parent, showInitially );

  // Start a thread to load all the dictionaries

  LoadDictionaries loadDicts( cfg );

  QObject::connect( &loadDicts, &LoadDictionaries::indexingDictionarySignal, &init, &Initializing::indexing );

  QEventLoop localLoop;

  QObject::connect( &loadDicts, &QThread::finished, &localLoop, &QEventLoop::quit );

  loadDicts.start();

  localLoop.exec();

  loadDicts.wait();

  if ( loadDicts.getExceptionText().size() )
  {
    QMessageBox::critical( parent, QCoreApplication::translate( "LoadDictionaries", "Error loading dictionaries" ),
                           QString::fromUtf8( loadDicts.getExceptionText().c_str() ) );

    return;
  }

  dictionaries = loadDicts.getDictionaries();

  // Helper function that will add a vector of dictionary::Class to the dictionary list
  // Implemented as lambda to access method's `dictionaries` variable
  auto  static addDicts = [&dictionaries](const vector< sptr< Dictionary::Class >> &dicts) {
    std::move(dicts.begin(), dicts.end(), std::back_inserter(dictionaries));
  };

  ///// We create transliterations synchronously since they are very simple

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  addDicts(Chinese::makeDictionaries( cfg.transliteration.chinese ));
#endif

  addDicts(Romaji::makeDictionaries( cfg.transliteration.romaji ));


  // Make Russian transliteration
  if ( cfg.transliteration.enableRussianTransliteration )
    dictionaries.push_back( RussianTranslit::makeDictionary() );

  // Make German transliteration
  if ( cfg.transliteration.enableGermanTransliteration )
    dictionaries.push_back( GermanTranslit::makeDictionary() );

  // Make Greek transliteration
  if ( cfg.transliteration.enableGreekTransliteration )
    dictionaries.push_back( GreekTranslit::makeDictionary() );

  // Make Belarusian transliteration
  if ( cfg.transliteration.enableBelarusianTransliteration )
  {
    addDicts(BelarusianTranslit::makeDictionaries());
  }

  addDicts(MediaWiki::makeDictionaries( loadDicts, cfg.mediawikis, dictNetMgr ));
  addDicts(WebSite::makeDictionaries( cfg.webSites, dictNetMgr ));
  addDicts(Forvo::makeDictionaries( loadDicts, cfg.forvo, dictNetMgr ));
  addDicts(Lingua::makeDictionaries( loadDicts, cfg.lingua, dictNetMgr ));
  addDicts(Programs::makeDictionaries( cfg.programs ));
  addDicts(VoiceEngines::makeDictionaries( cfg.voiceEngines ));
  addDicts(DictServer::makeDictionaries( cfg.dictServers ));


  GD_DPRINTF( "Load done\n" );

  // Remove any stale index files

  set< string > ids;
  std::pair< std::set< string >::iterator, bool > ret;

  for( unsigned x = dictionaries.size(); x--; )
  {
    ret = ids.insert( dictionaries[ x ]->getId() );
    if( !ret.second )
    {
      gdWarning( R"(Duplicate dictionary ID found: ID=%s, name="%s", path="%s")",
                 dictionaries[ x ]->getId().c_str(),
                 dictionaries[ x ]->getName().c_str(),
                 dictionaries[ x ]->getDictionaryFilenames().empty() ?
                   "" : dictionaries[ x ]->getDictionaryFilenames()[ 0 ].c_str()
                );
    }
  }

  QDir indexDir( Config::getIndexDir() );

  QStringList allIdxFiles = indexDir.entryList( QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks );

  for( QStringList::const_iterator i = allIdxFiles.constBegin(); i != allIdxFiles.constEnd(); ++i )
  {
    if( i->size() >= 32 && ids.find( FsEncoding::encode( i->left( 32 ) ) ) == ids.end() )
    {
      if( QFile::exists( *i ) )
      {
        indexDir.remove( *i );
      }
      else
      {
        // must be folder .
        auto dirPath = Utils::Path::combine( Config::getIndexDir(), *i );
        QDir t( dirPath );
        t.removeRecursively();
      }
    }
  }

  // Run deferred inits

  if ( doDeferredInit_ )
    doDeferredInit( dictionaries );
}

void doDeferredInit( std::vector< sptr< Dictionary::Class > > & dictionaries )
{
  for( unsigned x = 0; x < dictionaries.size(); ++x )
    dictionaries[ x ]->deferredInit();
}
