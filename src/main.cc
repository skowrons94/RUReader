#include <iostream>
#include <string>
#include <map>

#include <CAENDigitizerType.h>

#include "RUReader.h"

static void show_usage(std::string name)
{
  std::cerr << "Usage: "
            << "\t-h,--help\tShow this help message.\n"
            << "\t-d,--dgtz\tSpecify board name and board id.\n"
	    << "\t-i,--in\t\tSpecify the input file.\n"
	    << "\t-o,--out\tSpecify the output file."
            << std::endl;
}

int main( int argc, char* argv[] ){

  int bId;
  std::string fileIn    = "";
  std::string fileOut   = "";
  std::string dgtzName  = "";
  std::map<int,std::string> dgtz;

  std::string temp;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "-h") || (arg == "--help")) {
      show_usage(argv[0]);
      return 0;
    } else if ((arg == "-d") || (arg == "--dgtz")) {
      if (i + 2 < argc && std::string(argv[i+1])[0] != std::string("-")[0]
	               && std::string(argv[i+2])[0] != std::string("-")[0]) {
	dgtzName  = argv[++i];
	bId       = atoi( argv[++i] );
	dgtz[bId] = dgtzName;
      } else {
	std::cerr << "-d --dgtz options requires two arguments." << std::endl;
	return 1;
      }  
    } else if ((arg == "-i") || (arg == "--in")) {
      if (i + 1 < argc && std::string(argv[i+1])[0] != std::string("-")[0]) {
	fileIn = argv[++i];
      } else {
	std::cerr << "-i --in options requires one argument." << std::endl;
	return 1;
      }
    } else if ((arg == "-o") || (arg == "--out")) {
      if (i + 1 < argc && std::string(argv[i+1])[0] != std::string("-")[0]) {
	fileOut = argv[++i];
      } else {
	std::cerr << "-o --out options requires one argument." << std::endl;
	return 1;
      }
    } else ++i;
     
  };

  if( dgtzName != "" ){
    RUReader reader( dgtz );
    if( fileIn != "" ){
      if( fileOut != "" ){
	reader.Read( fileIn, fileOut );
	reader.Write( );
      }
      else{
	std::cerr << "No output file specified." << std::endl;
	return 1;
      }
    }
    else{
      std::cerr << "No input file specified." << std::endl;
      return 1;
    }
  }
  else {
    std::cerr << "No digitizer specified." << std::endl;
    return 1;
  }

  return 0;
  
}

