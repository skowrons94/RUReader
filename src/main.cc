#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <CAENDigitizerType.h>

#include "RUReader.h"

namespace fs = std::filesystem;

static void ShowUsage( const std::string& name )
{
  std::cerr
    << "Usage: " << name << " -i <input> -o <output.root> [options]\n\n"
    << "  -i, --in <path>          Input .caendat file or a directory containing them.\n"
    << "  -o, --out <path>         Output ROOT file.\n"
    << "  -d, --dgtz <name> <id>   Override the board type read from the file header.\n"
    << "                           May be repeated. Not required: the header already\n"
    << "                           describes every board of the acquisition.\n"
    << "  -w, --wave <1|2> <id>    When a board records two traces per event but only one is being kept\n"
    << "                           (i.e force-dual-trace is not given), select which physical wave (1 or 2)\n"
    << "                           to keep for board <id>. May be repeated, one board per use. Default: 1.\n"
    << "  -t, --ts-unit <unit>     Unit of the Timestamp branch: ps (default), ns, us,\n"
    << "                           ms, s or raw (the bare counter of the board).\n"
    << "  -a, --algo <name>        ROOT compression algorithm: zlib(default), lzma, lz4, zstd.\n"
    << "  -c, --compression <0-9>  ROOT compression level (default: the ROOT setting).\n"
    << "  -b, --buffer <MB>        Size of the read buffer, in MB (default: 64).\n"
    << "  -v, --verbose            Print more information, twice for every event.\n"
    << "      --ignore-fail        Ignore the board failure flag without asking.\n"
    << "      --force-dual-trace   Always create two trace branches in the TTree.\n"
    << "      --ignore-psd-boards  Do not write the events of PSD firmware boards.\n"
    << "  -h, --help               Show this help message.\n"
    << std::endl;
}

// Collects the .caendat files to convert, either the single file given by the
// user or every file of a directory, sorted by name so that the temporal order
// of a run split over several files is preserved.
static std::vector<std::string> FindCaendatFiles( const std::string& path )
{
  std::vector<std::string> files;

  std::error_code ec;

  if( fs::is_regular_file( path, ec ) ){
    files.push_back( path );
    return files;
  }

  if( !fs::is_directory( path, ec ) ){
    std::cerr << "ERROR: '" << path << "' is neither a file nor a directory!" << std::endl;
    return files;
  }

  std::cout << "Scanning directory for .caendat files..." << std::endl;

  for( const auto& entry : fs::directory_iterator( path, ec ) ){
    if( !entry.is_regular_file( ) ) continue;
    if( entry.path( ).extension( ) == ".caendat" ) files.push_back( entry.path( ).string( ) );
  }

  std::sort( files.begin( ), files.end( ) );

  if( files.empty( ) ){
    std::cerr << "ERROR: No .caendat files found in directory '" << path << "'!" << std::endl;
  }
  else{
    std::cout << "Found " << files.size( ) << " .caendat file(s) to process:" << std::endl;
    for( size_t i = 0; i < files.size( ); ++i )
      std::cout << "  " << ( i + 1 ) << ". " << fs::path( files[i] ).filename( ).string( )
                << std::endl;
  }

  return files;
}

// True when the argument at index i exists and is not another option.
static bool HasValue( int argc, char* argv[], int i )
{
  return i < argc && argv[i][0] != '-';
}

int main( int argc, char* argv[] )
{
  std::string fileIn;
  std::string fileOut;
  std::map<int,std::string> dgtz;
  std::map<int,int> waveSelect;

  bool ignoreFail       = false;
  bool forceDual        = false;
  bool ignorePsdBoards  = false;
  int  verbose          = 0;
  int  compressAlgo     = -1;
  std::string compressAlgoName = "default (zlib)";
  int  compression      = -1;
  uint64_t bufferBytes  = 64ull * 1024 * 1024;
  TimeUnit timeUnit     = TimeUnit::Picosecond;

  for( int i = 1; i < argc; ++i ){

    const std::string arg = argv[i];

    if( arg == "-h" || arg == "--help" ){
      ShowUsage( argv[0] );
      return 0;
    }
    else if( arg == "-d" || arg == "--dgtz" ){
      if( !HasValue( argc, argv, i + 1 ) || !HasValue( argc, argv, i + 2 ) ){
        std::cerr << "-d --dgtz requires two arguments: <board name> <board id>." << std::endl;
        return 1;
      }
      const std::string name = argv[++i];
      const int boardId = std::atoi( argv[++i] );
      if( boardId < 0 || boardId >= RUReader::kMaxBoards ){
        std::cerr << "ERROR: board id " << boardId << " is outside the range 0-"
                  << RUReader::kMaxBoards - 1 << "." << std::endl;
        return 1;
      }
      dgtz[boardId] = name;
    }
    else if( arg == "-w" || arg == "--wave" ){
      if( !HasValue( argc, argv, i + 1 ) || !HasValue( argc, argv, i + 2 ) ){
        std::cerr << "-w --wave requires two arguments: <wave 1|2> <board id>." << std::endl;
      }
      const int wave    = std::atoi(argv[++i]);
      const int boardId = std::atoi(argv[++i]);
      if( wave != 1 && wave !=2 ){
        std::cerr << "ERROR: --wave expects 1 or 2, got " << wave << "." << std::endl;
        return 1;
      }
      if( boardId < 0 || boardId >= RUReader::kMaxBoards ){
        std::cerr << "ERROR: board id " << boardId << " is outside the range 0 - ";
        std::cerr << RUReader::kMaxBoards - 1 << "." << std::endl;
        return 1;
      }
      waveSelect[boardId] = wave;
    }
    else if( arg == "-i" || arg == "--in" ){
      if( !HasValue( argc, argv, i + 1 ) ){
        std::cerr << "-i --in requires one argument." << std::endl;
        return 1;
      }
      fileIn = argv[++i];
    }
    else if( arg == "-o" || arg == "--out" ){
      if( !HasValue( argc, argv, i + 1 ) ){
        std::cerr << "-o --out requires one argument." << std::endl;
        return 1;
      }
      fileOut = argv[++i];
    }
    else if( arg == "-t" || arg == "--ts-unit" ){
      if( !HasValue( argc, argv, i + 1 ) ){
        std::cerr << "-t --ts-unit requires one argument." << std::endl;
        return 1;
      }
      const std::string unit = argv[++i];
      if( !ParseTimeUnit( unit, timeUnit ) ){
        std::cerr << "ERROR: unknown time stamp unit '" << unit
                  << "'. Use ps, ns, us, ms, s or raw." << std::endl;
        return 1;
      }
    }
    else if( arg == "-a" || arg == "--algo" ){
      if( !HasValue( argc, argv, i + 1 ) ){
        std::cerr << "-a --algo requires one argument." << std::endl;
        return 1;
      }
      const std::string name = argv[++i];
      if      ( name == "zlib" ) compressAlgo = ROOT::RCompressionSetting::EAlgorithm::kZLIB;
      else if ( name == "lzma" ) compressAlgo = ROOT::RCompressionSetting::EAlgorithm::kLZMA;
      else if ( name == "lz4"  ) compressAlgo = ROOT::RCompressionSetting::EAlgorithm::kLZ4;
      else if ( name == "zstd" ) compressAlgo = ROOT::RCompressionSetting::EAlgorithm::kZSTD;
      else{
        std::cerr << "ERROR: unknown algorithm '" << name << "'. use zlib, lzma, lz4, or zstd."
                  << std::endl;
        return 1;
      }
      compressAlgoName = name;
    }
    else if( arg == "-c" || arg == "--compression" ){
      if( !HasValue( argc, argv, i + 1 ) ){
        std::cerr << "-c --compression requires one argument." << std::endl;
        return 1;
      }
      compression = std::atoi( argv[++i] );
      if( compression < 0 || compression > 9 ){
        std::cerr << "ERROR: the compression level must be between 0 and 9." << std::endl;
        return 1;
      }
    }
    else if( arg == "-b" || arg == "--buffer" ){
      if( !HasValue( argc, argv, i + 1 ) ){
        std::cerr << "-b --buffer requires one argument." << std::endl;
        return 1;
      }
      const int megabytes = std::atoi( argv[++i] );
      if( megabytes < 1 || megabytes > 1024 ){
        std::cerr << "ERROR: the buffer size must be between 1 and 1024 MB." << std::endl;
        return 1;
      }
      bufferBytes = static_cast<uint64_t>( megabytes ) * 1024 * 1024;
    }
    else if( arg == "-v" || arg == "--verbose" ){
      ++verbose;
    }
    else if( arg == "--ignore-fail" ){
      ignoreFail = true;
    }
    else if( arg == "--force-dual-trace" ){
      forceDual = true;
    }
    else if( arg == "--ignore-psd-boards" ){
      ignorePsdBoards = true;
    }
    else{
      std::cerr << "Unknown option: " << arg << std::endl;
      ShowUsage( argv[0] );
      return 1;
    }
  }

  if( fileIn.empty( ) ){
    std::cerr << "No input file specified." << std::endl;
    return 1;
  }

  if( fileOut.empty( ) ){
    std::cerr << "No output file specified." << std::endl;
    return 1;
  }

  const std::vector<std::string> inputFiles = FindCaendatFiles( fileIn );
  if( inputFiles.empty( ) ){
    std::cerr << "ERROR: No input files found." << std::endl;
    return 1;
  }

  std::cout<<"Using compressAlgo " << compressAlgoName <<" at level "<<compression<<std::endl;

  RUReader reader( dgtz, ignoreFail, forceDual, ignorePsdBoards, waveSelect );
  reader.SetTimeUnit( timeUnit );
  reader.SetVerbose( verbose );
  reader.SetCompressionAlgorithm( compressAlgo );
  reader.SetCompression( compression );
  reader.SetBufferSize( bufferBytes );

  if( !reader.ReadMultiple( inputFiles, fileOut ) ){
    std::cerr << "\nConversion failed." << std::endl;
    reader.Write( );          // keep whatever could be decoded
    return 1;
  }

  reader.Write( );

  return 0;
}
