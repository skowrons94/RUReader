#include <iostream>
#include <fstream>
#include <stdio.h>
#include <string.h>
#include <bitset>
#include <sstream>

#include <CAENDigitizerType.h>

#include "RUReader.h"
#include "Utils.h"

RUReader::RUReader( std::map<int,std::string> name ){
 
  dgtzName = name;
  
}

RUReader::~RUReader(){

  // Clean up
  fEnergy.clear();
  fQshort.clear();
  fQlong.clear();
  
}

void RUReader::InitializeROOT( ){

  // Initializing ROOT objects
  //std::cout << "Initializing ROOT objects..." << std::endl;

  // Creating ROOT file
  fileOut = new TFile( fileOutName.c_str( ), "RECREATE" ); 

  for( auto it : dgtzChans ){ 

    int bIdx     = it.first;
    int channels = it.second;

    for(UInt_t j = 0 ; j < channels; j++){
      if( dgtzDppType[bIdx] == CAEN_DGTZ_DPPFirmware_PHA ){
        fEnergy[bIdx].push_back(new TH1F(Form("hEnergy_%i_%i",bIdx,j),
			          Form("Energy Board %i Channel %i",bIdx,j),
			          32768,0,32768));
        fEnergy[bIdx].back()->SetXTitle("Energy [channels]");
        fEnergy[bIdx].back()->SetYTitle("Counts / channel");
      }
      else if( dgtzDppType[bIdx] == CAEN_DGTZ_DPPFirmware_PSD ){
        fQshort[bIdx].push_back(new TH1F(Form("hQshort_%i_%i",bIdx,j),
			          Form("Qshort Board %i Channel %i",bIdx,j),
			          32768,0,32768));
        fQlong[bIdx].push_back(new TH1F(Form("hQlong_%i_%i",bIdx,j),
			          Form("Qlong Board %i Channel %i",bIdx,j),
			          65535,0,65535));
      
        fQshort[bIdx].back()->SetXTitle("Qshort [channels]");
        fQshort[bIdx].back()->SetYTitle("Counts / channel");
        fQlong[bIdx].back()->SetXTitle("Qlong [channels]");
        fQlong[bIdx].back()->SetYTitle("Counts / channel");
      }

    }

    if( dgtzDppType[bIdx] == CAEN_DGTZ_DPPFirmware_PHA ){
      fTree[bIdx] = new TTree(dgtzName[bIdx].c_str( ),dgtzName[bIdx].c_str( ));
      fTree[bIdx]->Branch( "pu"       ,        &fPu[bIdx],        "pu/O" );
      fTree[bIdx]->Branch( "channel"  ,   &fChannel[bIdx],   "channel/s" );
      fTree[bIdx]->Branch( "timeStamp", &fTimeStamp[bIdx], "timeStamp/l" );
      fTree[bIdx]->Branch( "energy"   ,  &fEnergies[bIdx],    "energy/s" );
      fTree[bIdx]->Branch( "extras"   ,    &fExtras[bIdx],    "extras/i" );
      fTree[bIdx]->Branch( "extras2"  ,   &fExtras2[bIdx],   "extras2/i" );
      fTree[bIdx]->SetMaxVirtualSize(1000000000LL);
    }
    else if( dgtzDppType[bIdx] == CAEN_DGTZ_DPPFirmware_PSD ){
      fTree[bIdx] = new TTree(dgtzName[bIdx].c_str( ),dgtzName[bIdx].c_str( ));
      fTree[bIdx]->Branch( "pu"       ,        &fPu[bIdx],        "pu/O" );
      fTree[bIdx]->Branch( "channel"  ,   &fChannel[bIdx],   "channel/s" );
      fTree[bIdx]->Branch( "timeStamp", &fTimeStamp[bIdx], "timeStamp/l" );
      fTree[bIdx]->Branch( "qShort"   ,    &fQShort[bIdx],    "qShort/s" );
      fTree[bIdx]->Branch( "qLong"    ,     &fQLong[bIdx],     "qLong/s" );
      fTree[bIdx]->Branch( "extras"   ,    &fExtras[bIdx],    "extras/i" );
      fTree[bIdx]->SetMaxVirtualSize(1000000000LL);
    }

  }

}

void RUReader::InitializeWave( int bIdx, uint32_t length, bool fDual ){

  //std::cout << "Initializing the waves..." << std::endl;

  fWave[bIdx] = true;
  
  if( fDual ){

    fWave1[bIdx].resize( length );
    fWave2[bIdx].resize( length );
    fDigital11[bIdx].resize( length );
    fDigital12[bIdx].resize( length );
    fDigital21[bIdx].resize( length );
    fDigital22[bIdx].resize( length );
    
    fTree[bIdx]->Branch( "fWave1",          &fWave1[bIdx] );
    fTree[bIdx]->Branch( "fWave2",          &fWave2[bIdx] );
    fTree[bIdx]->Branch( "fDigital1_1", &fDigital11[bIdx] );
    fTree[bIdx]->Branch( "fDigital1_2", &fDigital12[bIdx] );
    fTree[bIdx]->Branch( "fDigital2_1", &fDigital21[bIdx] );
    fTree[bIdx]->Branch( "fDigital2_2", &fDigital22[bIdx] );
    
  }

  else{
 
    fWave1[bIdx].resize( 2*length );
    fDigital11[bIdx].resize( 2*length );
    fDigital21[bIdx].resize( 2*length );

    fTree[bIdx]->Branch( "fWave",         &fWave1[bIdx] );
    fTree[bIdx]->Branch( "fDigital1", &fDigital11[bIdx] );
    fTree[bIdx]->Branch( "fDigital2", &fDigital21[bIdx] );
    
  }
  
}

void RUReader::UnpackHeader( uint32_t* inpBuffer, uint32_t& aggLength, uint32_t& board, std::bitset<8>& channelMask, uint32_t& offset ){

  // Aggregate buffer has a header containing different information
  aggLength                  = inpBuffer[0+offset]&0x0FFFFFFF;
  board                      = (inpBuffer[1+offset]&0xF8000000)>>27;
  boardFailFlag              = (inpBuffer[1+offset]&0x4000000)<<5 ;
  channelMask                = inpBuffer[1+offset]&0xFF;
  aggregateCounter           = inpBuffer[2+offset]&0x7FFFFF;
  aggregateTimeTag           = inpBuffer[3+offset];

}

void RUReader::UnpackPHA( uint32_t* inpBuffer, uint32_t& board, std::bitset<8>& channelMask, uint32_t& offset ){

  // We need to reconstruct the channel number from the couple id for the 
  // 16 channels boards
  index = 0;
  for(bit = 0 ; bit < 8 ; bit++){ 
    if(channelMask.test(bit)){
      couples[index]=bit;
      index++;
    }
  }

  // Getting the DataForm
  dataForm = frames[board];
  uint32_t pos;

  // Variables to store the data
  startingPos         = 4 + offset;
  coupleAggregateSize = 0;
  extras              = 0;
  extras2             = 0;    
  energy              = 0;
  tstamp              = 0;
  fineTS              = 0;
  flags               = 0;
  pur                 = false;
  satu                = false;
  lost                = false;

  // Here starts the real decoding of the data for the channels
  chanNum = 0; 
  for(cpl = 0 ; cpl < channelMask.count() ; cpl++){

    // Channel aggregate size is board dependant
    pos    = startingPos;
    format = dataForm.GetFormat( "Size" );
    coupleAggregateSize = project_range( format.first, format.second, std::bitset<32>( inpBuffer[pos] ) ).to_ulong( );

    dataForm.SetDataFormat(inpBuffer[startingPos+1]);
    
    for(evt = 0 ; evt < (coupleAggregateSize-2)/dataForm.EvtSize();evt++){
	      
      // PU and Energy seems not to be dependant on the board
      pur      = static_cast<bool>(((inpBuffer[startingPos+dataForm.EvtSize()-1+dataForm.EvtSize()*evt+2])&0x8000)>>15);
      energy   = static_cast<uint16_t>((inpBuffer[startingPos+dataForm.EvtSize()-1+dataForm.EvtSize()*evt+2])&0x7FFF);
        
      // Time stamp is board dependant
      pos    = startingPos+2+dataForm.EvtSize()*evt;
      format = dataForm.GetFormat( "TS" );
      tstamp = static_cast<uint64_t>(project_range( format.first, format.second, std::bitset<32>(inpBuffer[pos])).to_ulong());

      // Channel number is board dependant
      if( dataForm.CheckFormat( "CH" ) ){
        chanNum  = static_cast<uint32_t>(((inpBuffer[startingPos+2+dataForm.EvtSize()*evt]&0x80000000)>>31)+couples[cpl]*2);
      }
      else{
        chanNum = couples[cpl];
      }

      // Extras size is board dependant
      pos     = startingPos+dataForm.EvtSize()-1+dataForm.EvtSize()*evt+2;
      format  = dataForm.GetFormat( "Extras" );
      extras  = static_cast<uint16_t>(project_range( format.first, format.second, std::bitset<32>( inpBuffer[pos] ) ).to_ulong( ));

      // If Roll Over flag is set, we need to add it
      if( (extras&0002) ){
        ++RO[board][chanNum];
      }

      // Adding RO to the Time Stamp
      format = dataForm.GetFormat( "TS" );
      //      std::cout << tstamp << "  " << format.second << "  " << RO[board] << std::endl;
      tstamp = ((uint64_t)RO[board][chanNum]<<(format.second+1) | (uint64_t)tstamp );

      // The Lost and Satu flags seems not to be dependant on the board
      lost  = (extras&0x20)>>5;
      satu  = (extras&0x1);
      
      if(dataForm.CheckEnabled( "Extras" )){
	extras2 = inpBuffer[startingPos+dataForm.EvtSize()-2+dataForm.EvtSize()*evt+2];
	switch(dataForm.GetConfig( "Extras" )) {
	case 0: // Extended Time Stamp [31:16] ; baseline*4 [15:0]
	  tstamp = ((uint64_t)(extras2&0xFFFF0000))<<15 | (uint64_t)tstamp;
	  break;
	case 1: // Extended Time stamp [31:16] ; flags [15:0]
	  tstamp = ((uint64_t)(extras2&0xFFFF0000))<<15 | (uint64_t)tstamp;
	  break;
	case 2: // Extended Time stamp [31:16] ; Flags [15:10] ; Fine Time Stamp [9:0]
	  fineTS = static_cast<uint16_t>(extras2&0x3FF);
	  tstamp = ((uint64_t)(extras2&0xFFFF0000))<<15 | (uint64_t)tstamp;
	  break;
	case 4: // Lost Trigger Counter [31:16] ; Total Trigger [15:0]
	  break;
	case 5: // Positive zero crossing [31:16] ; Negative zero crossing [15:0]
	  break;
	case 7: // Debug fixed value;
	  break;
	default:
	  break;
	}
	
      }
      else extras2 = 0;

      if( dataForm.CheckEnabled( "Trace" ) ){
        UnpackWave( inpBuffer, board, dataForm, startingPos+2+dataForm.EvtSize()*evt);
      }

      // Filling ROOT files
      fEnergy[board][chanNum]->Fill(energy);
      
      fPu[board]        = pur;
      fEnergies[board]  = energy;
      fTimeStamp[board] = tstamp;
      fChannel[board]   = chanNum;
      fExtras[board]    = extras;
      fExtras2[board]   = extras2;
      fTree[board]->Fill( );

    }

    startingPos+=coupleAggregateSize;

  }

}

void RUReader::UnpackPSD( uint32_t* inpBuffer, uint32_t& board, std::bitset<8>& channelMask, uint32_t& offset ){
  
  // We need to reconstruct the channel number from the couple id for the 
  // 16 channels boards
  int index = 0 ;
  uint8_t couples[8]={0,0,0,0,0,0,0,0};
  for(uint16_t bit = 0 ; bit < 8 ; bit++){ 
    if(channelMask.test(bit)){
      couples[index]=bit;
      index++;
    }
  }

  // Getting the DataForm
  DataFrame dataForm = frames[board];
  std::pair<int,int> format;
  uint32_t pos;

  // Variables to store the data
  uint32_t startingPos         = 4 + offset;
  uint32_t coupleAggregateSize = 0;
  uint64_t tstamp              = 0;
  uint32_t fineTS              = 0;
  uint32_t extras              = 0;
  uint32_t flags               = 0;
  uint16_t qshort              = 0;
  uint16_t qlong               = 0;
  bool pur                     = false;
  bool ovr                     = false;
  bool lost                    = false;
  
  // Here starts the real decoding of the data for the channels 
  uint32_t chanNum = 0;
  for(uint8_t cpl = 0 ; cpl < channelMask.count() ; cpl++){
    
    // Channel aggregate size is board dependant
    pos    = startingPos;
    format = dataForm.GetFormat( "Size" );
    coupleAggregateSize = project_range( format.first, format.second, std::bitset<32>( inpBuffer[pos] ) ).to_ulong( );

    dataForm.SetDataFormat(inpBuffer[startingPos+1]);
    
    for(uint16_t evt = 0 ; evt < (coupleAggregateSize-2)/dataForm.EvtSize();evt++){

      // PU and Charges seems not to be dependant on the board
      pur    = static_cast<bool>(((inpBuffer[startingPos+dataForm.EvtSize()-1+dataForm.EvtSize()*evt+2])&0x8000)>>15);
      qshort = static_cast<uint16_t>((inpBuffer[startingPos+dataForm.EvtSize()-1+dataForm.EvtSize()*evt+2])&0x7FFF);
      qlong  = static_cast<uint16_t>((inpBuffer[startingPos+dataForm.EvtSize()-1+dataForm.EvtSize()*evt+2])>>16);

      // Time stamp is board dependant
      pos    = startingPos+2+dataForm.EvtSize()*evt;
      format = dataForm.GetFormat( "TS" );
      tstamp = static_cast<uint64_t>(project_range( format.first, format.second, std::bitset<32>(inpBuffer[pos])).to_ulong());

      // Channel number is board dependant
      if( dataForm.CheckFormat( "CH" ) ){
        chanNum  = static_cast<uint32_t>(((inpBuffer[startingPos+2+dataForm.EvtSize()*evt]&0x80000000)>>31)+couples[cpl]*2);
      }
      else{
        chanNum = couples[cpl];
      }
      
      if(dataForm.CheckEnabled( "Extras" )){
	      extras = inpBuffer[startingPos+dataForm.EvtSize()-2+dataForm.EvtSize()*evt+2];
        if(dataForm.CheckConfig( "Extras" )){
	        switch(dataForm.GetConfig( "Extras" )) {
	          case 0: // Extended Time Stamp [31:16] ; baseline*4 [15:0]
	            tstamp = (static_cast<uint64_t>((extras&0xFFFF0000))<<15) | tstamp;
	            break;
	          case 1: // Extended Time stamp [31:16] ; flags [15:0]
	            ovr    = static_cast<bool>((extras&0x00004000)>>14);
	            lost   = static_cast<bool>((extras&0x00001000)>>12);
	            flags  = static_cast<uint32_t>((extras&0x0000F000)<<15);
	            tstamp = (static_cast<uint64_t>((extras&0xFFFF0000))<<15) | tstamp;
	            break;
	          case 2: // Extended Time stamp [31:16] ; Flags [15:10] ; Fine Time Stamp [9:0]
	            flags  = static_cast<uint32_t>((extras&0x0000F000)<<15);
	            fineTS = static_cast<uint16_t>(extras&0x000003FF);
	            tstamp = ((uint64_t)(extras)&0xFFFF0000)<<15 | tstamp;
	            break;
	          case 4: // Lost Trigger Counter [31:16] ; Total Trigger [15:0]
	            break;
	          case 5: // Positive zero crossing [31:16] ; Negative zero crossing [15:0]
	            break;
	          case 7: // Debug fixed value;
	            break;
	          default:
	            break;
	        }

        }
        else tstamp = (static_cast<uint64_t>((extras&0x00007FFF))<<32) | tstamp;

      }

      if( dataForm.CheckEnabled( "Trace" ) ){
        UnpackWave( inpBuffer, board, dataForm, startingPos+2+dataForm.EvtSize()*evt);
      }

      // Filling ROOT files
      fQshort[board][chanNum]->Fill(qshort);
      fQlong[board][chanNum]->Fill(qlong);
      
      fPu[board]        = pur;
      fQShort[board]    = qshort;
      fQLong[board]     = qlong;
      fTimeStamp[board] = tstamp;
      fChannel[board]   = chanNum;
      fExtras[board]    = extras;
      fTree[board]->Fill( );

    }
      
    startingPos+=coupleAggregateSize;

  }

}

void RUReader::UnpackWave(uint32_t* inpBuffer, uint32_t& board, DataFrame& dataForm, int pos ){

  uint16_t numSamples = dataForm.GetConfig( "NumSamples" )/2;
  int samplePos = pos;

  std::pair<int,int> formatSample = dataForm.GetFormat( "Sample" );
  std::pair<int,int> formatDP1    = dataForm.GetFormat( "DP1" );
  std::pair<int,int> formatDP2    = dataForm.GetFormat( "DP2" );

  if( !fWave[board] ){
    InitializeWave( board, numSamples, dataForm.CheckEnabled( "DT" ) );
  }
  for( int idx = 0; idx < numSamples; ++idx ){
    ++samplePos;
    if( dataForm.CheckEnabled( "DT" ) ){
      fWave1[board][idx]     = static_cast<short>(project_range( formatSample.first   , formatSample.second   , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fWave2[board][idx]     = static_cast<short>(project_range( formatSample.first+16, formatSample.second+16, std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital11[board][idx] = static_cast<bool>(project_range(  formatDP1.first      , formatDP1.second      , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital12[board][idx] = static_cast<bool>(project_range(  formatDP1.first+16   , formatDP1.second+16   , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital21[board][idx] = static_cast<bool>(project_range(  formatDP2.first      , formatDP2.second      , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital21[board][idx] = static_cast<bool>(project_range(  formatDP2.first+16   , formatDP2.second+16   , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
    }
    else{
      fWave1[board][2*idx]       = static_cast<short>(project_range( formatSample.first   , formatSample.second   , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fWave1[board][2*idx+1]     = static_cast<short>(project_range( formatSample.first+16, formatSample.second+16, std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital11[board][2*idx]   = static_cast<short>(project_range( formatDP1.first      , formatDP1.second      , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital11[board][2*idx+1] = static_cast<short>(project_range( formatDP1.first+16   , formatDP1.second+16   , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital21[board][2*idx]   = static_cast<short>(project_range( formatDP2.first      , formatDP2.second      , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
      fDigital21[board][2*idx+1] = static_cast<short>(project_range( formatDP2.first+16   , formatDP2.second+16   , std::bitset<32>(inpBuffer[samplePos])).to_ulong());
    }

  }

}

uint64_t RUReader::ReadHeader( std::ifstream& input ){

  char*     header;
  uint32_t  hSize;

  input.get( (char*)&hSize, sizeof(uint32_t) );

  header = new char[(hSize)*sizeof(uint32_t)];

  input.seekg(0, std::ios::beg);
  input.get( header, (hSize)*sizeof(uint32_t) );

  uint32_t* inpBuffer = (uint32_t*)header;

  int nboards = inpBuffer[2];

  startTime = inpBuffer[5];

  uint32_t boardInfo, nsPerSample, nsPerTimetag, dppVersion, channels, boardRegId;
  for( int i = 0; i < nboards; ++i ){
    boardInfo    = inpBuffer[6+i];
    nsPerSample  = static_cast<uint32_t>((boardInfo&0x0000003F));
    nsPerTimetag = static_cast<uint32_t>((boardInfo>>6)&0x0000003F);
    dppVersion   = static_cast<uint32_t>((boardInfo>>12)&0x0000000F);
    channels     = static_cast<uint32_t>((boardInfo>>16)&0x000000FF);
    boardRegId   = static_cast<uint32_t>((boardInfo>>24)&0x000000FF);

    // Creating the DataFrame for each board
    switch( dppVersion ){
      case 0: // PHA
        dgtzDppType[i] = CAEN_DGTZ_DPPFirmware_PHA;
        break;
      case 1: // PSD
        dgtzDppType[i] = CAEN_DGTZ_DPPFirmware_PSD;
        break;
    }
    dgtzChans[i] = channels;
    frames[i] = DataFrame( dgtzDppType[i], dgtzName[i] );
    frames[i].Build( );
    for( int ch = 0; ch < channels; ++ ch )
      RO[i][ch] = 0;
    fWave[i] = false;

  }

  InitializeROOT( );

  return hSize*sizeof(uint32_t);

}

void RUReader::ReadData( std::ifstream& input, uint64_t pos ){

  std::cout << "Starting reading data..." << std::endl;
    
  std::bitset<8> channelMask;
  uint64_t size, length, offset;
  uint32_t aggLength, board;

  input.seekg(0, std::ios::end);
  length = (uint64_t)input.tellg();

  uint64_t buffSize = 1e8;
  char*    data = new char[buffSize];

  double progress;
  ULong_t i, posBar, barWidth = 70;
  while( pos < length ){

    input.seekg( pos, std::ios::beg );
    input.read( data, buffSize );

    uint32_t offset = 0;
    while( offset < buffSize ){

      /* Progress Bar */
      progress = (double)(pos + offset*sizeof(uint32_t))/(double)length;
      if( (int)(progress*100) % 1 == 0 ){
        posBar = barWidth * progress;
        std::cout << "[";
        for( int k = 0; k < barWidth; ++k ){
	        if( k < posBar ) std::cout << "=";
	        else if( k == posBar ) std::cout << ">";
	        else std::cout << " ";
        }
        std::cout << "] " << int( progress * 100 ) << "%\r";
        std::cout.flush( );
      }

      if( ( pos + offset*sizeof(uint32_t) ) >= length ){
	      pos += offset*sizeof(uint32_t);
	      break;
      }

      UnpackHeader( (uint32_t*)data, aggLength, board, channelMask, offset );
      if( ( aggLength + offset )*sizeof(uint32_t) > buffSize ){
        pos += offset*sizeof(uint32_t);
        break;
      }

      if( dgtzDppType[board] == CAEN_DGTZ_DPPFirmware_PHA )
        UnpackPHA( (uint32_t*)data, board, channelMask, offset );
      else if( dgtzDppType[board] == CAEN_DGTZ_DPPFirmware_PSD )
        UnpackPSD( (uint32_t*)data, board, channelMask, offset );

      offset += aggLength;

    } 

  }

  std::cout << std::endl;

}

void RUReader::Read( std::string in, std::string out ){

  fileOutName = out;
  std::cout << "Starting reading header..." << std::endl;
  
  std::ifstream input( in.c_str( ), std::ios::binary );

  uint64_t pos = ReadHeader( input );
  std::cout << "Header read." << std::endl;
  ReadData( input, pos );

  std::cout << "Finished reading the input filer." << std::endl;
  
  input.close( );
  
}

void RUReader::Write( ){

  std::cout << "Saving data to ROOT file..." << std::endl;
  
  fileOut->cd( );

  if( !fEnergy.empty( ) ){
    fileOut->mkdir( "Energies" );
    for( auto it1 : fEnergy ){
      fileOut->cd( "Energies" );
      for( auto it2 : it1.second ){
        it2->Write( );
      }
    }
  }

  if( !fQlong.empty( ) ){
    fileOut->mkdir( "Qlong" );
    for( auto it1 : fQlong ){
      fileOut->cd( "Qlong" );
      for( auto it2 : it1.second ){
        it2->Write( );
      }
    }
  }

  if( !fQshort.empty( ) ){
    fileOut->mkdir( "Qshort" );
    for( auto it1 : fQshort ){
      fileOut->cd( "Qshort" );
      for( auto it2 : it1.second ){
        it2->Write( );
      }
    }
  }

  fileOut->cd( );
  TVectorD v(1);
  v[0] = startTime;
  v.Write( "StartEpoch" );

  fileOut->cd( );
  for( auto it : fTree ){
    it.second->Write( 0, TObject::kOverwrite );
  }

  fileOut->Close( );

  std::cout << "Saved data to ROOT file." << std::endl;

}
