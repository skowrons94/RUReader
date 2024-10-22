#include <string>
#include "DataFrameBuilderPHA.h"

DataFrameBuilderPHA::DataFrameBuilderPHA( std::string dgtzName ){
    
    this->fDgtzName = dgtzName;

    this->ProduceNumSamples( );
    this->ProduceFlags( );
    this->ProduceConfigs( );
    this->ProduceFormats( );
    
}

DataFrameBuilderPHA::~DataFrameBuilderPHA( ){
    
}

void DataFrameBuilderPHA::ProduceNumSamples( ){
    
    fNumSamples = 0;
    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos ){
        this->fNumSamples = 8;
    }
    else if( fDgtzName.find("724") != std::string::npos ||
             fDgtzName.find("781") != std::string::npos || 
             fDgtzName.find("782") != std::string::npos ){
        this->fNumSamples = 2;
    }
}

void DataFrameBuilderPHA::ProduceFlags( ){
    
    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos ){
      
        this->fFlags["DT"]     = 31; 
        this->fFlags["Energy"] = 30;
        this->fFlags["TS"]     = 29;
        this->fFlags["Extras"] = 28;
        this->fFlags["Trace"]  = 27;

    }
    else if( fDgtzName.find("724") != std::string::npos ||
             fDgtzName.find("781") != std::string::npos || 
             fDgtzName.find("782") != std::string::npos ){
      
        this->fFlags["DT"]     = 31; 
        this->fFlags["Energy"] = 29;
        this->fFlags["TS"]     = 28;
        this->fFlags["Trace"]  = 30;

    }
    
}

void DataFrameBuilderPHA::ProduceConfigs( ){
    
    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos ){
      
        this->fConfigs["Extras"]      = std::make_pair(24,26); 
        this->fConfigs["AnaProbe1"]   = std::make_pair(22,23);
        this->fConfigs["AnaProbe2"]   = std::make_pair(20,21);
        this->fConfigs["DigProbe"]    = std::make_pair(16,19);
        this->fConfigs["NumSamples"]  = std::make_pair(0,15);

    }
    else if( fDgtzName.find("724") != std::string::npos ||
             fDgtzName.find("781") != std::string::npos || 
             fDgtzName.find("782") != std::string::npos ){
      
        this->fConfigs["AnaProbe"]    = std::make_pair(22,23);
        this->fConfigs["DigProbe1"]   = std::make_pair(20,21);
        this->fConfigs["DigProbe2"]   = std::make_pair(16,19);
        this->fConfigs["NumSamples"]  = std::make_pair(0,15);

    }
    
}

void DataFrameBuilderPHA::ProduceFormats( ){

    if( fDgtzName.find("730") != std::string::npos ||
        fDgtzName.find("725") != std::string::npos ){
      
        this->fFormats["Size"]    = std::make_pair(0,30);
        this->fFormats["Energy"]  = std::make_pair(0,14); 
        this->fFormats["Extras"]  = std::make_pair(16,20);
        this->fFormats["TS"]      = std::make_pair(0,30);
        this->fFormats["CH"]      = std::make_pair(31,31);
        this->fFormats["Sample"]  = std::make_pair(0,13);
        this->fFormats["DP"]      = std::make_pair(14,14);
        this->fFormats["TR"]      = std::make_pair(15,15);

    }
    else if( fDgtzName.find("724") != std::string::npos ||
             fDgtzName.find("781") != std::string::npos || 
             fDgtzName.find("782") != std::string::npos ){
      
        this->fFormats["Size"]     = std::make_pair(0,20);
        this->fFormats["Energy"]   = std::make_pair(0,14);
        this->fFormats["Extras"]   = std::make_pair(16,23);
        this->fFormats["TS"]       = std::make_pair(0,29);
        this->fFormats["RO"]       = std::make_pair(31,31);
        this->fFormats["Sample"]   = std::make_pair(0,13);
        this->fFormats["DP"]       = std::make_pair(14,14);
        this->fFormats["TR"]       = std::make_pair(15,15);

    }

}