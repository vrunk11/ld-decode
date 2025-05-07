/************************************************************************

    decoder.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019-2021 Adam Sampson

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
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

#include "decoder.h"

#include "decoderpool.h"

qint32 Decoder::getLookBehind() const
{
    return 0;
}

qint32 Decoder::getLookAhead() const
{
    return 0;
}

DecoderThread::DecoderThread(QAtomicInt& _abort, DecoderPool& _decoderPool, QObject *parent)
    : QThread(parent), abort(_abort), decoderPool(_decoderPool), outputWriter(_decoderPool.getOutputWriter())
{
}

void DecoderThread::run()
{
    // Input and output data
    QVector<SourceField> inputFields;
    QVector<SourceField> chromaFields;
    QVector<ComponentFrame> componentFramesVideo;
    QVector<ComponentFrame> componentFramesChroma;
    QVector<OutputFrame> outputFrames;

    while (!abort) {
        // Get the next batch of fields to process
        qint32 startFrameNumber, startIndex, endIndex;
		if(decoderPool.isYC)
		{
			if (!decoderPool.getYCFrames(startFrameNumber, inputFields, chromaFields, startIndex, endIndex)) {
				// No more input frames -- exit
				break;
			}
		}
		else
		{
			if (!decoderPool.getInputFrames(startFrameNumber, inputFields, startIndex, endIndex)) {
				// No more input frames -- exit
				break;
			}
		}

        // Adjust the temporary arrays to the right size
        const qint32 numFrames = (endIndex - startIndex) / 2;
        componentFramesVideo.resize(numFrames);
        componentFramesChroma.resize(numFrames);
        outputFrames.resize(numFrames);
		
		// Decode the fields to component frames
		if(decoderPool.isYC)
		{
			// decode the chroma
			decodeFrames(chromaFields, startIndex, endIndex, componentFramesChroma);
			// scale the luma
			decoderPool.getDecoderAsMono().decodeFrames(inputFields, startIndex, endIndex, componentFramesVideo);
			
		}
		else
		{
			// decode cvbs
			decodeFrames(inputFields, startIndex, endIndex, componentFramesVideo);
		}

        // Convert the component frames to the output format and size
        for (qint32 i = 0; i < numFrames; i++) {
			if(decoderPool.isYC)
			{
				componentFramesVideo[i].setU(*componentFramesChroma[i].getU());
				componentFramesVideo[i].setV(*componentFramesChroma[i].getV());
			}
            outputWriter.convert(componentFramesVideo[i], outputFrames[i]);
        }

        // Write the frames to the output file
        if (!decoderPool.putOutputFrames(startFrameNumber, outputFrames)) {
            abort = true;
            break;
        }
    }
}
