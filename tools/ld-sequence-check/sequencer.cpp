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

Sequencer::Sequencer(int _idThread,QVector<qint32>& _threadOk, int _maxThreads, QAtomicInt& _abort, SequencingPool& _sequencingPool, QObject *parent)
    : QThread(parent), idThread(_idThread), threadOk(_threadOk), maxThreads(_maxThreads), abort(_abort), sequencingPool(_sequencingPool)
{
}

void Sequencer::run()
{
    // Variables for getInputFrame
    qint32 frameNumber;
	
	QVector<QVector<qint32>> sequenceFieldSeqNo;
	QVector<QVector<SourceVideo::Data>> sequenceSourceField;
	QVector<QVector<LdDecodeMetaData::Field>> sequenceFieldMetadata;
	
	sequenceFieldSeqNo.resize(10);
	sequenceSourceField.resize(10);
	sequenceFieldMetadata.resize(10);
	
	//other variables
	long offset;
	bool isPal = 0;
	bool isCav = 0;
	bool noPhase = 0;
	bool blank = 0;
	int nbFieldValid = 0;
	VbiData vbiData;
	
	sequencingPool.getParameters(offset, isCav, noPhase, blank);
	
    while(!abort) {
        // Get the next field to process from the input file
        if (!sequencingPool.getInputFrameSequence(idThread, frameNumber, nbFieldValid, sequenceFieldSeqNo, sequenceSourceField, sequenceFieldMetadata, videoParameters))
		{
            // No more input fields -- exit
			qInfo() << "(Sequencer) end of TBC...";
            break;
        }
		// get video standard
		isPal = (videoParameters[0].system == PAL) ? 1 : 0 ;
		
		//analyse and set number for each frame
		Sequencer::sequenceCheck(frameNumber, isPal, noPhase, sequenceFieldSeqNo, sequenceSourceField, sequenceFieldMetadata, &vbiData, videoParameters);
		
		//if not at the end
		if((sequencingPool.getLastFrameNumber() - frameNumber) > 5)
		{
			nbFieldValid = 8;//write only 4 frame
		}
		qInfo() << "(Sequencer) nb_field_valid : " << nbFieldValid << " LastFrameNumber : " << sequencingPool.getLastFrameNumber() << " frame number : " << frameNumber;
		qInfo() << "(Sequencer) f1 : " << vbiData.vbiNumber[0];
		qInfo() << "(Sequencer) f2 : " << vbiData.vbiNumber[1];
		qInfo() << "(Sequencer) f3 : " << vbiData.vbiNumber[2];
		qInfo() << "(Sequencer) f4 : " << vbiData.vbiNumber[3];
		qInfo() << "(Sequencer) f5 : " << vbiData.vbiNumber[4];
		
		//write each availeble field
		for(int i=0;i <= nbFieldValid;i+=2)
		{
			if(blank)
			{
				blankVbi(sequenceSourceField[i],isPal,videoParameters[0]);
				blankVbi(sequenceSourceField[i+1],isPal,videoParameters[0]);
			}
			
			if(i != nbFieldValid || nbFieldValid <= 5)
			{
				//insert only if its possible
				if(frameNumber +(i/2) + offset > 0)
				{
					if(!Sequencer::generate24BitCode(&vbiData,vbiData.vbiNumber[i/2] + offset,isCav,isPal))
					{
						Sequencer::encode24BitManchester(sequenceSourceField[i],&vbiData,isCav,videoParameters[0]);
					}
				}
				sequencingPool.setOutputFrame(frameNumber+(i/2), sequenceSourceField[i][0], sequenceSourceField[i+1][0],
										sequenceFieldSeqNo[i][0], sequenceFieldSeqNo[i+1][0]);
			}
		}				
    }
	qInfo() << "(Sequencer) end of processing";
}

//analyse and attribute frame number to each frame
void Sequencer::sequenceCheck(long frameNumber, bool isPal, bool noPhase, QVector<QVector<qint32>> sequenceFieldSeqNo, QVector<QVector<SourceVideo::Data>> sequenceSourceField, QVector<QVector<LdDecodeMetaData::Field>> sequenceFieldMetadata, VbiData* vbiData, QVector<LdDecodeMetaData::VideoParameters>& videoParameters)
{
	int fieldTreated = 8;//4frame
	int precedingThread = Sequencer::idThread -1;
	
	int phaseId = 0;
	int phaseId2 = 0;
	int phaseOffset = 0;
	bool endOfVideo = 0;
	
	if(maxThreads > 1)
	{
		if(idThread == 0)
		{
			precedingThread = maxThreads-1;
		}
		threadOk[idThread] = 4;//waiting sequence analysis from previous thread
		while(threadOk[precedingThread] < 6 && frameNumber > 1){}//wait other threads to finish
		threadOk[idThread] = 5;//running
	}
	
	//get latest frame number
	int latestFrameNumber = sequencingPool.getLatestFrameNumber();
	
	//if we are at the end treat the last frame
	if((frameNumber + 4) >=  sequencingPool.getLastFrameNumber())//actual frame + 4 = 5 frame
	{
		fieldTreated = (5 - ((frameNumber + 4) - sequencingPool.getLastFrameNumber()))*2;
		endOfVideo = 1;
		qInfo() << "(sequence check) frame number : " << frameNumber << "  field treated : " << fieldTreated;
	}
	
	//sequence analysis
	for(int i = 0; i < fieldTreated;i+=2)
	{
		//if phase check not deactivated
		if(!noPhase && (i < (fieldTreated - (endOfVideo*2))))//disable sequence check on last frame (if endOfVideo = 1) then block 1 frame earlier)
		{
			phaseId = getPhaseId(sequenceSourceField[i], isPal , videoParameters[0]);
			phaseId2 = getPhaseId(sequenceSourceField[i+2], isPal , videoParameters[0]);
			
			//check if there is chroma burst
			if(phaseId != 0 && phaseId2 != 0)
			{
				phaseOffset = mesurePhaseOffset(isPal,phaseId,phaseId2);
				
				if(isPal)
				{
					if(phaseOffset == 0)
					{
						vbiData->vbiNumber[i/2] = latestFrameNumber -1;//same number as last frame (asuming repeated frame)
						qInfo() << "(sequence check) frame repeat detected with phase (" << phaseId << "/" << phaseId2 << ") at frame " << frameNumber + (i/2);
					}
					else if(phaseOffset == 1)
					{
						vbiData->vbiNumber[i/2] = latestFrameNumber;
						latestFrameNumber++;
					}
					else if(phaseOffset == 2)
					{
						vbiData->vbiNumber[i/2] = latestFrameNumber +1;
						latestFrameNumber += 2;
						qInfo() << "(sequence check) skip detected with phase (" << phaseId << "/" << phaseId2 << ") at frame " << frameNumber + (i/2);
					}
					else if(phaseOffset == 3)
					{
						vbiData->vbiNumber[i/2] = latestFrameNumber +2;
						latestFrameNumber += 3;
						qInfo() << "(sequence check) skip detected with phase (" << phaseId << "/" << phaseId2 << ") at frame " << frameNumber + (i/2);
					}
				}
				else//ntsc
				{
					if(phaseOffset == 0)
					{
						//asume a skip of 1 frame
						vbiData->vbiNumber[i/2] = latestFrameNumber +1;
						latestFrameNumber += 2;
						qInfo() << "(sequence check) skip detected with phase (" << phaseId << "/" << phaseId2 << ") at frame " << frameNumber + (i/2);
					}
					else if(phaseOffset == 1)
					{
						vbiData->vbiNumber[i/2] = latestFrameNumber;
						latestFrameNumber++;
					}
				}
			}
			else
			{
				vbiData->vbiNumber[i/2] = latestFrameNumber;
				latestFrameNumber++;
				qInfo() << "(sequence check) no phase detected";
			}
		}
		else
		{
			vbiData->vbiNumber[i/2] = latestFrameNumber;
			latestFrameNumber++;
			qInfo() << "(sequence check) phase detection disabled";
		}
	}
	
	//set latest frame number
	sequencingPool.setLatestFrameNumber(latestFrameNumber);
	
	threadOk[idThread] = 6;//set to ready	
}

int Sequencer::generate24BitCode(VbiData* vbiData,long frameNumber,bool isCav,bool isPal)
{
	//frameNumber --;//remove 1 to start at frame 0
	
	int bitCode16 = 0;
	int bitCode17 = 0;
	int bitCode18 = 0;
	int hexValues[2][5];
	
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
			if(isPal)
			{
				qCritical() << "CAV Frame number too high (exeding : 99999 frame) consider using CLV timecode instead...";
			}
			else
			{
				qCritical() << "CAV Frame number too high (exeding : 79999 frame) consider using CLV timecode instead...";
			}
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
		if((frameNumber > 1079999 && !isPal) || (frameNumber > 899975 && isPal))
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
	
	bool bitValue[6];//store 
	
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

//get phase from chroma burst
int Sequencer::getPhaseId(QVector<SourceVideo::Data> sequenceSourceField, int isPal, LdDecodeMetaData::VideoParameters& videoParameters)
{
	int value = 0;
	int phaseId = 0;
	int tmp[4];
	int blackLvl = 0;
	
	if(isPal)
	{
		//get black levels from video to be compatible with chroma file
		blackLvl = sequenceSourceField[0][(videoParameters.fieldWidth * (5)) + videoParameters.colourBurstStart - 6];
		
		//read 2 line to compare value
		tmp[0] = sequenceSourceField[0][(videoParameters.fieldWidth * (75)) + videoParameters.colourBurstStart + 17];
		tmp[1] = sequenceSourceField[0][(videoParameters.fieldWidth * (76)) + videoParameters.colourBurstStart + 17];
		
		tmp[2] = sequenceSourceField[0][(videoParameters.fieldWidth * (75)) + videoParameters.colourBurstStart + 17 + 2];
		tmp[3] = sequenceSourceField[0][(videoParameters.fieldWidth * (76)) + videoParameters.colourBurstStart + 17 + 2];
		
		
		//qInfo() << "(phaseID) (" << tmp[0] << "/" << tmp[1] << "/" << tmp[2] << "/" << tmp[3] << ")";
		//find the patern
		if(tmp[0] - tmp[2] > 15 || tmp[2] - tmp[0] > 15 || tmp[0] - tmp[2] < -15 || tmp[2] - tmp[0] < -15)//if value are close to each other = noBurst
		{
			if(tmp[0] > blackLvl && tmp[1] > blackLvl)//tmp[0] = high / tmp[1] = high
			{
				phaseId = 1;
			}
			else if(tmp[0] > blackLvl && tmp[1] < blackLvl)//tmp[0] = high / tmp[1] = low
			{
				phaseId = 2;
			}
			else if(tmp[0] < blackLvl && tmp[1] < blackLvl)//tmp[0] = low / tmp[1] = low
			{
				phaseId = 3;
			}
			else//tmp[0] = low / tmp[1] = high
			{
				phaseId = 4;
			}
		}
		else
		{
			//value close to each other = noBurst
			phaseId = 0;
			qInfo() << "(phaseID) no burst detected";
		}
	}
	else
	{
		tmp[0] = sequenceSourceField[0][(videoParameters.fieldWidth * 75) + videoParameters.colourBurstStart + 17];
		tmp[1] = sequenceSourceField[0][(videoParameters.fieldWidth * 75) + videoParameters.colourBurstStart + 17 + 2];
		//find the position of highest value
		if(tmp[0] - tmp[1] > 3840 || tmp[1] - tmp[0] > 3840 || tmp[0] - tmp[1] < -3840 || tmp[1] - tmp[0] < -3840)//if value are close to each other = noBurst (15 * 256 = 3840)
		{
			if(tmp[0] > tmp[1])
			{
				phaseId = 1;
			}
			else
			{
				phaseId = 2;
			}
		}
		else
		{
			//value close to each other = noBurst
			phaseId = 0;
			qInfo() << "(phaseID) no burst detected";
		}
	}
	return phaseId;
}

//get phase offset
int Sequencer::mesurePhaseOffset(int isPal, int phaseId, int phaseId2)
{
	if(isPal)
	{
		if(phaseId2 < phaseId)
		{
			return((phaseId2 + 4) - phaseId);
		}
		else
		{
			return((phaseId - phaseId2)*-1);
		}
	}
	else
	{
		if(phaseId == phaseId2)
		{
			return 0;
		}
		else
		{
			return 1;
		}
	}
	return 0;
}

void Sequencer::blankVbi(QVector<SourceVideo::Data>& fieldData, int isPal, LdDecodeMetaData::VideoParameters& videoParameters)
{
	//get black levels from video to be compatible with chroma file
	int blackLvl = fieldData[0][(videoParameters.fieldWidth * (12)) + videoParameters.colourBurstStart - 6];
	
	for(int i = videoParameters.activeVideoStart;i <= (videoParameters.fieldWidth - 6);i++)
	{
		if(!isPal)
		{
			fieldData[0].replace(i + (videoParameters.fieldWidth *10),blackLvl);
		}
		fieldData[0].replace(i + (videoParameters.fieldWidth *15),blackLvl);
		fieldData[0].replace(i + (videoParameters.fieldWidth *16),blackLvl);
		fieldData[0].replace(i + (videoParameters.fieldWidth *17),blackLvl);
	}
}