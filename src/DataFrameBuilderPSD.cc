#include <string>
#include "DataFrameBuilderPSD.h"

DataFrameBuilderPSD::DataFrameBuilderPSD( std::string dgtzName ){
    
    this->fDgtzName = dgtzName;

    this->ProduceNumSamples( );
    this->ProduceFlags( );
    this->ProduceConfigs( );
    this->ProduceFormats( );
    
}

DataFrameBuilderPSD::~DataFrameBuilderPSD( ){
    
}

void DataFrameBuilderPSD::ProduceNumSamples( ){
    
    fNumSamples = 0;
    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos || 
        fDgtzName.find("720") != std::string::npos ){
        this->fNumSamples = 8;
    }

}

void DataFrameBuilderPSD::ProduceFlags( ){
    
    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos ){
      
        this->fFlags["DT"]     = 31; 
        this->fFlags["Charge"] = 30;
        this->fFlags["TS"]     = 29;
        this->fFlags["Extras"] = 28;
        this->fFlags["Trace"]  = 27;

    }
    else if( fDgtzName.find("720") != std::string::npos ){
      
        this->fFlags["DT"]     = 31; 
        this->fFlags["Charge"] = 30;
        this->fFlags["TS"]     = 29;
	    this->fFlags["Extras"] = 28;
	    this->fFlags["Trace"]  = 27;
        this->fFlags["EP"]     = 26;
	    this->fFlags["EET"]    = 22;

    }
    
}

void DataFrameBuilderPSD::ProduceConfigs( ){
    
    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos ){
      
        this->fConfigs["Extras"]      = std::make_pair(24,26); 
        this->fConfigs["AnaProbe"]    = std::make_pair(22,23);
        this->fConfigs["DigProbe1"]   = std::make_pair(16,18);
        this->fConfigs["DigProbe2"]   = std::make_pair(19,21);
        this->fConfigs["NumSamples"]  = std::make_pair(0,15);

    }
    else if( fDgtzName.find("720") != std::string::npos ){

        this->fConfigs["TrgMd"]       = std::make_pair(24,25); 
        this->fConfigs["DigProbe3"]   = std::make_pair(19,21);
        this->fConfigs["DigProbe4"]   = std::make_pair(16,18);
        this->fConfigs["NumSamples"]  = std::make_pair(0,11);

    }
    
}

void DataFrameBuilderPSD::ProduceFormats( ){

    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos ){
      
        this->fFormats["Size"]     = std::make_pair(0,21);
        this->fFormats["Charge1"]  = std::make_pair(0,14);
	    this->fFormats["Charge2"]  = std::make_pair(16,31); 
        this->fFormats["Extras"]   = std::make_pair(0,31);
        this->fFormats["TS"]       = std::make_pair(0,30);
        this->fFormats["CH"]       = std::make_pair(31,31);
        this->fFormats["Sample"]   = std::make_pair(0,13);
        this->fFormats["DP1"]      = std::make_pair(14,14);
        this->fFormats["DP2"]      = std::make_pair(15,15);

    }
    else if( fDgtzName.find("720") != std::string::npos ){
      
        this->fFormats["Size"]    = std::make_pair(0,23);
        this->fFormats["Charge1"] = std::make_pair(0,14);
	    this->fFormats["Charge2"] = std::make_pair(16,31); 
        this->fFormats["Extras"]  = std::make_pair(0,31);
        this->fFormats["TS"]      = std::make_pair(0,31);
        this->fFormats["Sample"]  = std::make_pair(0,11);
        this->fFormats["DP1"]     = std::make_pair(12,12);
        this->fFormats["DP2"]     = std::make_pair(13,13);
        this->fFormats["DP3"]     = std::make_pair(14,14);
        this->fFormats["DP4"]     = std::make_pair(15,15);

    }

}
