/************************************************************************

    sequencer.cpp

    ld-sequence-check - video sequence analysis for ld-decode
    Copyright (C) 2024-2024 Vrunk11

    This file is part of ld-decode-tools.

    ld-sequence-check is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include "sequencer.h"
#include "sequencingpool.h"

Sequencer::Sequencer(QAtomicInt& _abort, SequencingPool& _sequencingPool, QObject *parent)
    : QThread(parent), abort(_abort), sequencingPool(_sequencingPool)
{
}

void Sequencer::run()
{
    // Variables for getInputFrame
    qint32 frameNumber;
    QVector<qint32> firstFieldSeqNo;
    QVector<qint32> secondFieldSeqNo;
    QVector<SourceVideo::Data> firstSourceField;
    QVector<SourceVideo::Data> secondSourceField;
    QVector<LdDecodeMetaData::Field> firstFieldMetadata;
    QVector<LdDecodeMetaData::Field> secondFieldMetadata;
    QVector<qint32> availableSourcesForFrame;
	bool reverse;
	bool isPal = 0;
	bool isCav = 0;
	VbiData vbiData;
	
    while(!abort) {
        // Get the next field to process from the input file
        if (!sequencingPool.getInputFrame(frameNumber, firstFieldSeqNo, firstSourceField, firstFieldMetadata,
                                       secondFieldSeqNo, secondSourceField, secondFieldMetadata,
                                       videoParameters, reverse, availableSourcesForFrame)) {
            // No more input fields -- exit
			qInfo() << "(Sequencer) end of TBC...";
            break;
        }
		isPal = (videoParameters[0].system == PAL) ? 1 : 0 ;
        // Initialise the output fields and process sources to output
        SourceVideo::Data outputFirstField(firstSourceField[0].size());
        SourceVideo::Data outputSecondField(secondSourceField[0].size());
		
		//insert only if its possible
		if(!Sequencer::generate24BitCode(&vbiData,frameNumber,isCav,isPal))
		{
			Sequencer::encode24BitManchester(firstSourceField,&vbiData,isCav,videoParameters[0]);
		}
		
		sequencingPool.setOutputFrame(frameNumber, firstSourceField[0], secondSourceField[0],
                                    firstFieldSeqNo[0], secondFieldSeqNo[0]);
    }
	qInfo() << "(Sequencer) end of processing";
}

int Sequencer::generate24BitCode(VbiData* vbiData,int frameNumber,bool isCav,bool isPal)
{
	int bitCode16 = 0;
	int bitCode17 = 0;
	int bitCode18 = 0;
	int hexValues[1][5];//it loose value [0][4] if i didnt set the size to [1][5] and i don't know why
	
	//clv time
	int hour = 0;
	int minute = 0;
	int second = 0;
	
	int i = 0;
	int y = 0;
	
	if(isCav)
	{
		if((frameNumber > 79999 && !isPal) || (frameNumber > 99999 && isPal))
		{
			qCritical() << "CAV Frame number too high consider using CLV timecode instead...";
			return 1;
		}
		
		//split each figure into an hex value
		hexValues[1][4] = (frameNumber % 10);
		hexValues[1][3] = (frameNumber / 10) % 10;
		hexValues[1][2] = (frameNumber / 100) % 10;
		hexValues[1][1] = (frameNumber / 1000) % 10;
		hexValues[1][0] = (frameNumber / 10000) % 10;
		
		//add the key (F)
		bitCode17 = 0xF;
		bitCode18 = 0xF;
		
		//add each number as 4bit value
		for(int i = 0; i < 5; i++)
		{
			bitCode17 = bitCode17 << 4;
			bitCode18 = bitCode18 << 4;
			bitCode17 += hexValues[1][i];
			bitCode18 += hexValues[1][i];
		}
	}
	else//CLV
	{
		if((frameNumber > 1079970 && !isPal) || (frameNumber > 899975 && isPal))
		{
			qCritical() << "CLV Frame number too high : exeding 9h 59min 59sec";
			return 1;
		}
		
		if(isPal)
		{
			while(frameNumber >= 90000)//25 (frame) * 60 (sec) * 60 (min)
			{
				hour++;
				frameNumber -= 90000;
			}
			while(frameNumber >= 1500)//25 (frame) * 60 (sec)
			{
				minute++;
				frameNumber -= 1500;
			}
			while(frameNumber >= 25)//25 (frame)
			{
				second++;
				frameNumber -= 25;
			}
		}
		else
		{
			while(frameNumber >= 108000)//30 (frame) * 60 (sec) * 60 (min)
			{
				hour++;
				frameNumber -= 108000;
			}
			while(frameNumber >= 1800)//30 (frame) * 60 (sec)
			{
				minute++;
				frameNumber -= 1800;
			}
			while(frameNumber >= 30)//30 (frame)
			{
				second++;
				frameNumber -= 30;
			}
		}
			
		//split each figure into an hex value (line 16)
		hexValues[0][4] = (frameNumber % 10);
		hexValues[0][3] = (frameNumber / 10) % 10;
		hexValues[0][2] = (second % 10);
		hexValues[0][1] = (0xE);
		hexValues[0][0] = ((second / 10) % 10) + 10;
		
		//split each figure into an hex value (line 17-18)
		hexValues[1][4] = (minute % 10);
		hexValues[1][3] = (minute / 10) % 10;
		hexValues[1][2] = (0xD);
		hexValues[1][1] = (0xD);
		hexValues[1][0] = (hour);
		
		//add the diferent key
		bitCode16 = 0x8;
		bitCode17 = 0xF;
		bitCode18 = 0xF;
		
		//add each number as 4bit value
		for(int i = 0; i < 5; i++)
		{
			bitCode16 = bitCode16 << 4;
			bitCode17 = bitCode17 << 4;
			bitCode18 = bitCode18 << 4;
			
			bitCode16 += hexValues[0][i];
			bitCode17 += hexValues[1][i];
			bitCode18 += hexValues[1][i];
		}
	}
	//transform bitcode to binary array
	for(i = 0,y = 23; i < 24; i++,y--)
	{
		vbiData->dataL16[y] = (bitCode16 >> i) & 1;
		vbiData->dataL17[y] = (bitCode17 >> i) & 1;
		vbiData->dataL18[y] = (bitCode18 >> i) & 1;
	}
	return 0;
}

//encode and write in the vbi
void Sequencer::encode24BitManchester(QVector<SourceVideo::Data> &fieldData,VbiData* bitCode,bool isCav,const LdDecodeMetaData::VideoParameters& videoParameters)
{
	// Get the number of samples for 2µs
	long sizeBits = (videoParameters.sampleRate / 1000000) * 2;
	long nbSample = (sizeBits*24);
	long startLinePos = (15 + isCav) * videoParameters.fieldWidth;//position of the line corresponding to the picture encoding
	unsigned short manchesterData[nbSample-1];//replace by video data start of active area pointer
	bool isOdd = ((sizeBits % 2) == 0) ? 0 : 1 ;

	int i = 0;
	int y = 0;
	
	bool bitValue[5];//store 
	
	//for each bit
	for(i = 0; i < 24;i++)
	{
		bitValue[0] = bitCode->dataL16[i];
		bitValue[1] = bitCode->dataL17[i];
		bitValue[2] = bitCode->dataL18[i];
		
		/*bitValue[3] = bitCode.dataL16F2[i];
		bitValue[4] = bitCode.dataL17F2[i];
		bitValue[5] = bitCode.dataL18F2[i];*/
		
		//for each line
		for(int b = isCav;b < 3;b++)
		{
			//bit = 1
			if(bitValue[b])
			{
				for(y = 0;y < (sizeBits - isOdd)/2;y++)//first half (low)
				{
					fieldData[0].replace((videoParameters.fieldWidth * (b - isCav)) + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.black16bIre);
				}
				
				for(y = (sizeBits - isOdd)/2;y < (sizeBits + isOdd);y++)//2nd half (high)
				{
					fieldData[0].replace((videoParameters.fieldWidth *(b - isCav)) + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.white16bIre);
				}
			}
			else//bit = 0
			{
				for(y = 0;y < (sizeBits - isOdd)/2;y++)//first half (high)
				{
					fieldData[0].replace((videoParameters.fieldWidth * (b - isCav)) + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.white16bIre);
				}
				
				for(y = (sizeBits - isOdd)/2;y < (sizeBits + isOdd);y++)//2nd half (low)
				{
					fieldData[0].replace((videoParameters.fieldWidth * (b - isCav)) + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.black16bIre);
				}
			}
		}
	}
}