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
		sequencingPool.setOutputFrame(frameNumber, firstSourceField[0], secondSourceField[0],
                                    firstFieldSeqNo[0], secondFieldSeqNo[0]);
		//qInfo() << "(Sequencer) loop test2";
    }
	qInfo() << "(Sequencer) end of processing";
}