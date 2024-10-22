#include "Utils.h"
#include "DataFrame.h"

DataFrame::DataFrame( CAEN_DGTZ_DPPFirmware_t dppType, std::string dgtzName ){

    switch( dppType ){
        case( CAEN_DGTZ_DPPFirmware_PHA ):
            this->SetBuilder( new DataFrameBuilderPHA( dgtzName ) );
            break;
        case( CAEN_DGTZ_DPPFirmware_PSD ):
            this->SetBuilder( new DataFrameBuilderPSD( dgtzName ) );
            break;
        default:
            break;
    }

    fDgtzName = dgtzName;
    
}

DataFrame::~DataFrame( ){

}

void DataFrame::Produce( ){

    this->fBuilder->ProduceFlags( );
    this->fBuilder->ProduceConfigs( );
    this->fBuilder->ProduceFormats( );

}

void DataFrame::Build( ){

    this->Produce( );

    fFlags   = this->fBuilder->GetFlags( );
    fConfigs = this->fBuilder->GetConfigs( );
    fFormats = this->fBuilder->GetFormat( );
    fNumSamples = this->fBuilder->GetNumSamples( );

}

bool DataFrame::CheckEnabled( std::string key ){

    if( CheckKey( fFlags, key ) )
        return this->fDataFormat.test(fFlags[key]);
    else
        return false;

}

uint16_t DataFrame::GetConfig( std::string key ){
    std::size_t min = (std::size_t)fConfigs[key].first;
    std::size_t max = (std::size_t)fConfigs[key].second;
    if( key == "NumSamples" )
        return static_cast<uint16_t>(project_range(min,max,fDataFormat).to_ulong())*fNumSamples;
    else
        return static_cast<uint16_t>(project_range(min,max,fDataFormat).to_ulong());
}

std::pair<int,int>& DataFrame::GetFormat( std::string key ){
    return fFormats[key];
}

bool DataFrame::CheckFormat( std::string key ){
    return CheckKey( fFormats, key );
}

bool DataFrame::CheckConfig( std::string key ){
    return CheckKey( fConfigs, key );
}

uint16_t& DataFrame::EvtSize( ){

    if( evtSize == 0 ){
        if(CheckEnabled("Energy")) evtSize +=1;
        if(CheckEnabled("Charge")) evtSize +=1;
        if(CheckEnabled("TS"))     evtSize +=1;
        if(CheckEnabled("Extras") &&
            fDgtzName.find("724") == std::string::npos &&
            fDgtzName.find("781") == std::string::npos && 
            fDgtzName.find("782") == std::string::npos) evtSize +=1;
        if(CheckEnabled("Trace"))  evtSize += GetConfig( "NumSamples" )/2;
    }

    return evtSize;

}
