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
    bool reverse;
    QVector<qint32> availableSourcesForFrame;
	
	//qInfo() << "(Sequencer) Processing TBC...";
	
    while(!abort) {
        // Get the next field to process from the input file
        if (!sequencingPool.getInputFrame(frameNumber, firstFieldSeqNo, firstSourceField, firstFieldMetadata,
                                       secondFieldSeqNo, secondSourceField, secondFieldMetadata,
                                       videoParameters, reverse, availableSourcesForFrame)) {
            // No more input fields -- exit
			qInfo() << "(Sequencer) end of TBC...";
            break;
        }
		//qInfo() << "(Sequencer) loop test1";
        // Initialise the output fields and process sources to output
        SourceVideo::Data outputFirstField(firstSourceField[0].size());
        SourceVideo::Data outputSecondField(secondSourceField[0].size());

        /*stackField(frameNumber, firstSourceField, videoParameters[0], firstFieldMetadata, availableSourcesForFrame, noDiffDod, passThrough, outputFirstField, outputFirstFieldDropOuts);
        stackField(frameNumber, secondSourceField, videoParameters[0], secondFieldMetadata, availableSourcesForFrame, noDiffDod, passThrough, outputSecondField, outputSecondFieldDropOuts);*/

        // Return the processed fields
        /*sequencingPool.setOutputFrame(frameNumber, outputFirstField, outputSecondField,
                                    firstFieldSeqNo[0], secondFieldSeqNo[0]);*/
									
		Sequencer::encode24BitManchester(firstSourceField,Sequencer::generate24BitCode(1,frameNumber),videoParameters[0]);
		
		sequencingPool.setOutputFrame(frameNumber, firstSourceField[0], secondSourceField[0],
                                    firstFieldSeqNo[0], secondFieldSeqNo[0]);
		//qInfo() << "(Sequencer) loop test2";
    }
	qInfo() << "(Sequencer) end of processing";
}

int Sequencer::generate24BitCode(const int isCav,int frameNumber)
{
	char hexValues[4];
	int bitCode = 0;
	if(isCav)
	{
		if(frameNumber > 79999)
		{
			qCritical() << "Frame number too high consider using timecode instead...";
			return 0;
		}
		hexValues[4] = frameNumber % 10;
		hexValues[3] = (frameNumber / 10) % 10;
		hexValues[2] = (frameNumber / 100) % 10;
		hexValues[1] = (frameNumber / 1000) % 10;
		hexValues[0] = (frameNumber / 10000) % 10;
		
		//add the key (F)
		bitCode = 15;
		//add each number as individual exadecimal value
		for(int i = 0; i < 5; i++)
		{
			bitCode = bitCode << 4;
			bitCode += hexValues[i];
		}
	}
	return bitCode;
}

//encode and write in the vbi
void Sequencer::encode24BitManchester(QVector<SourceVideo::Data> &fieldData,int bitCode,const LdDecodeMetaData::VideoParameters& videoParameters)
{
	// Get the number of samples for 2µs
	long sizeBits = (videoParameters.sampleRate / 1000000) * 2;
	long nbSample = (sizeBits*24);
	unsigned short manchesterData[nbSample-1];//replace by video data start of active area pointer
	unsigned char bitsArray[24];
	char isOdd = ((sizeBits % 2) == 0) ? 0 : 1 ;
	double startLinePos = 16 * videoParameters.fieldWidth;
	
	int i = 0;
	int y = 0;
	
	//transform bitcode to binary array
	for(i = 0,y = 23; i < 24; i++,y--)
	{
		bitsArray[y] = (bitCode >> i) & 1;
	}
	
	for(i = 0; i < 24;i++)
	{
		if(bitsArray[i])
		{
			for(y = 0;y < (sizeBits - isOdd)/2;y++)//first half (low)
			{
				fieldData[0].replace((i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.black16bIre);
				fieldData[0].replace(videoParameters.fieldWidth + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.black16bIre);
			}
			
			for(y = (sizeBits - isOdd)/2;y < (sizeBits + isOdd);y++)//2nd half (high)
			{
				fieldData[0].replace((i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.white16bIre);
				fieldData[0].replace(videoParameters.fieldWidth + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.white16bIre);
			}
		}
		else
		{
			for(y = 0;y < (sizeBits - isOdd)/2;y++)//first half (high)
			{
				fieldData[0].replace((i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.white16bIre);
				fieldData[0].replace(videoParameters.fieldWidth + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.white16bIre);
			}
			
			for(y = (sizeBits - isOdd)/2;y < (sizeBits + isOdd);y++)//2nd half (low)
			{
				fieldData[0].replace((i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.black16bIre);
				fieldData[0].replace(videoParameters.fieldWidth + (i * sizeBits) + startLinePos + y + videoParameters.activeVideoStart,videoParameters.black16bIre);
			}
		}
	}
}

/*
// Private method to get a single scanline of greyscale data
SourceVideo::Data Sequencer::getFieldLine(const SourceVideo::Data &sourceField, qint32 fieldLine, int quantity
                                               const LdDecodeMetaData::VideoParameters& videoParameters)
{
    // Range-check the field line
    if (fieldLine < startFieldLine || fieldLine > endFieldLine) {
        qWarning() << "Cannot generate field-line data, line number is out of bounds! Scan line =" << fieldLine;
        return SourceVideo::Data();
    }

    qint32 startPointer = (fieldLine - startFieldLine) * videoParameters.fieldWidth;
    return sourceField.mid(startPointer, videoParameters.fieldWidth * quantity);
}*/