/************************************************************************

    sequencingpool.cpp

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

#include "sequencingpool.h"
#include "vbidecoder.h"

SequencingPool::SequencingPool(QString _outputFilename, QString _outputJsonFilename,
                             qint32 _maxThreads, QVector<LdDecodeMetaData *> &_ldDecodeMetaData, QVector<SourceVideo *> &_sourceVideos,
                             bool _isCav, long _offset, QObject *parent)
    : QObject(parent), outputFilename(_outputFilename), outputJsonFilename(_outputJsonFilename),
      maxThreads(_maxThreads), isCav(_isCav), offset(_offset),
      abort(false), ldDecodeMetaData(_ldDecodeMetaData), sourceVideos(_sourceVideos)
{
}

bool SequencingPool::process()
{
    qInfo() << "Performing final sanity checks...";
    // Open the target video
    targetVideo.setFileName(outputFilename);
    if (outputFilename == "-") {
        if (!targetVideo.open(stdout, QIODevice::WriteOnly)) {
                // Could not open stdout
                qInfo() << "Unable to open stdout";
                return false;
        }
    } else {
        if (!targetVideo.open(QIODevice::WriteOnly)) {
                // Could not open target video file
                qInfo() << "Unable to open output video file";
                return false;
        }
    }

    // Show some information for the user
    qInfo() << "Using" << maxThreads << "threads to process" << ldDecodeMetaData[0]->getNumberOfFrames() << "frames";

    // Initialise processing state
    inputFrameNumber = 1;
    outputFrameNumber = 1;
    lastFrameNumber = ldDecodeMetaData[0]->getNumberOfFrames();
    totalTimer.start();

    // Start a vector of decoding threads to process the video
    qInfo() << "Beginning multi-threaded video analysis process...";
    QVector<QThread *> threads;
    threads.resize(maxThreads);
    for (qint32 i = 0; i < maxThreads; i++) {
        threads[i] = new Sequencer(abort, *this);
        threads[i]->start(QThread::LowPriority);
    }

    // Wait for the workers to finish
    for (qint32 i = 0; i < maxThreads; i++) {
        threads[i]->wait();
        delete threads[i];
    }

    // Did any of the threads abort?
    if (abort) {
        targetVideo.close();
        return false;
    }

    // Show the processing speed to the user
    double totalSecs = (static_cast<double>(totalTimer.elapsed()) / 1000.0);
    qInfo() << "Sequence analysis complete -" << lastFrameNumber << "frames in" << totalSecs << "seconds (" <<
               lastFrameNumber / totalSecs << "FPS )";

    qInfo() << "Creating JSON metadata file for analysed TBC...";
    correctMetaData().write(outputJsonFilename);

    // Close the target video
    targetVideo.close();

    return true;
}

void SequencingPool::getParameters(long& _offset,bool& _isCav)
{
	_isCav = isCav;
	_offset = offset;
}

// Get the next frame that needs processing from the input.
//
// Returns true if a frame was returned, false if the end of the input has been
// reached.
bool SequencingPool::getInputFrameSequence(qint32& frameNumber,int& nbFieldValid,
                                  QVector<QVector<qint32>>& fieldNumber, QVector<QVector<SourceVideo::Data>>& fieldVideoData, QVector<QVector<LdDecodeMetaData::Field>>& fieldMetadata,
                                  QVector<LdDecodeMetaData::VideoParameters>& videoParameters)
{
    QMutexLocker locker(&inputMutex);

    if (inputFrameNumber > lastFrameNumber) {
        // No more input frames
        return false;
    }
	
	nbFieldValid = (lastFrameNumber - inputFrameNumber)*2;
	
	if(nbFieldValid > 10)
	{
		nbFieldValid = 10;
	}

    frameNumber = inputFrameNumber;
    inputFrameNumber+=4;

    // Determine the number of sources available (included padded sources)
    qint32 numberOfSources = sourceVideos.size();

    qDebug().nospace() << "Processing sequential frame number #" <<
                          frameNumber << " to " << frameNumber + 4 << "/" << lastFrameNumber << " from " << numberOfSources << " possible source(s)";
	
    // Prepare the vectors
    for(int i = 0;i <= nbFieldValid;i+=2)
	{
		if(i != nbFieldValid || nbFieldValid <= 5)
		{
			fieldNumber[i].resize(numberOfSources);
			fieldNumber[i+1].resize(numberOfSources);
			
			fieldVideoData[i].resize(numberOfSources);
			fieldVideoData[i+1].resize(numberOfSources);
			
			fieldMetadata[i].resize(numberOfSources);
			fieldMetadata[i+1].resize(numberOfSources);
			
			videoParameters.resize(numberOfSources);
			
			for (qint32 sourceNo = 0; sourceNo < numberOfSources; sourceNo++) {
				// Determine the fields for the input frame
				fieldNumber[i][sourceNo] = (frameNumber * 2) -1;
				fieldNumber[i+1][sourceNo] = (frameNumber * 2);
				
				// Fetch the input data (get the fields in TBC sequence order to save seeking)
				if (fieldNumber[i][sourceNo] < fieldNumber[i+1][sourceNo]) {
					fieldVideoData[i][sourceNo] = sourceVideos[sourceNo]->getVideoField(fieldNumber[i][sourceNo]+i);
					fieldVideoData[i+1][sourceNo] = sourceVideos[sourceNo]->getVideoField(fieldNumber[i+1][sourceNo]+i);
				} else {
					fieldVideoData[i+1][sourceNo] = sourceVideos[sourceNo]->getVideoField(fieldNumber[i+1][sourceNo]+i);
					fieldVideoData[i][sourceNo] = sourceVideos[sourceNo]->getVideoField(fieldNumber[i][sourceNo]+i);
				}

				fieldMetadata[i][sourceNo] = ldDecodeMetaData[sourceNo]->getField(fieldNumber[i][sourceNo]+i);
				fieldMetadata[i+1][sourceNo] = ldDecodeMetaData[sourceNo]->getField(fieldNumber[i+1][sourceNo]+i);
				videoParameters[sourceNo] = ldDecodeMetaData[sourceNo]->getVideoParameters();
			}
		}
	}
	
    return true;
}


// Put a corrected frame into the output stream.
//
// The worker threads will complete frames in an arbitrary order, so we can't
// just write the frames to the output file directly. Instead, we keep a map of
// frames that haven't yet been written; when a new frame comes in, we check
// whether we can now write some of them out.
//
// Returns true on success, false on failure.
bool SequencingPool::setOutputFrame(qint32 frameNumber,
                                   SourceVideo::Data firstTargetFieldData, SourceVideo::Data secondTargetFieldData,
                                   qint32 firstFieldSeqNo, qint32 secondFieldSeqNo)
{
    QMutexLocker locker(&outputMutex);

    // Put the output frame into the map
    OutputFrame pendingFrame;
    pendingFrame.firstTargetFieldData = firstTargetFieldData;
    pendingFrame.secondTargetFieldData = secondTargetFieldData;
    pendingFrame.firstFieldSeqNo = firstFieldSeqNo;
    pendingFrame.secondFieldSeqNo = secondFieldSeqNo;

    pendingOutputFrames[frameNumber] = pendingFrame;

    // Write out as many frames as possible
    while (pendingOutputFrames.contains(outputFrameNumber)) {
        const OutputFrame &outputFrame = pendingOutputFrames.value(outputFrameNumber);

        // Save the frame data to the output file (with the fields in the correct order)
        bool writeFail = false;
        if (outputFrame.firstFieldSeqNo < outputFrame.secondFieldSeqNo) {
            // Save the first field and then second field to the output file
            if (!writeOutputField(outputFrame.firstTargetFieldData)) writeFail = true;
            if (!writeOutputField(outputFrame.secondTargetFieldData)) writeFail = true;
        } else {
            // Save the second field and then first field to the output file
            if (!writeOutputField(outputFrame.secondTargetFieldData)) writeFail = true;
            if (!writeOutputField(outputFrame.firstTargetFieldData)) writeFail = true;
        }

        // Was the write successful?
        if (writeFail) {
            // Could not write to target TBC file
            qCritical() << "Writing fields to the output TBC file failed";
            targetVideo.close();
            return false;
        }

        // Show debug
        qDebug().nospace() << "Processed frame " << outputFrameNumber;

        if (outputFrameNumber % 100 == 0) {
            qInfo() << "Processed and written frame" << outputFrameNumber;
        }

        pendingOutputFrames.remove(outputFrameNumber);
        outputFrameNumber++;
    }

    return true;
}

// Write a field to the output file.
// Returns true on success, false on failure.
bool SequencingPool::writeOutputField(const SourceVideo::Data &fieldData)
{
    return targetVideo.write(reinterpret_cast<const char *>(fieldData.data()), 2 * fieldData.size());
}

LdDecodeMetaData &SequencingPool::correctMetaData()
{
    return *ldDecodeMetaData[0];
}
