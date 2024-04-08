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
                             bool _reverse, QObject *parent)
    : QObject(parent), outputFilename(_outputFilename), outputJsonFilename(_outputJsonFilename),
      maxThreads(_maxThreads), reverse(_reverse),
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

// Get the next frame that needs processing from the input.
//
// Returns true if a frame was returned, false if the end of the input has been
// reached.
bool SequencingPool::getInputFrame(qint32& frameNumber,
                                  QVector<qint32>& firstFieldNumber, QVector<SourceVideo::Data>& firstFieldVideoData, QVector<LdDecodeMetaData::Field>& firstFieldMetadata,
                                  QVector<qint32>& secondFieldNumber, QVector<SourceVideo::Data>& secondFieldVideoData, QVector<LdDecodeMetaData::Field>& secondFieldMetadata,
                                  QVector<LdDecodeMetaData::VideoParameters>& videoParameters,
                                  bool& _reverse,
                                  QVector<qint32>& availableSourcesForFrame)
{
    QMutexLocker locker(&inputMutex);

    if (inputFrameNumber > lastFrameNumber) {
        // No more input frames
        return false;
    }

    frameNumber = inputFrameNumber;
    inputFrameNumber++;

    // Determine the number of sources available (included padded sources)
    qint32 numberOfSources = sourceVideos.size();

    qDebug().nospace() << "Processing sequential frame number #" <<
                          frameNumber << " from " << numberOfSources << " possible source(s)";
	
    // Prepare the vectors
    firstFieldNumber.resize(numberOfSources);
    firstFieldVideoData.resize(numberOfSources);
    firstFieldMetadata.resize(numberOfSources);
    secondFieldNumber.resize(numberOfSources);
    secondFieldVideoData.resize(numberOfSources);
    secondFieldMetadata.resize(numberOfSources);
    videoParameters.resize(numberOfSources);
	
    for (qint32 sourceNo = 0; sourceNo < numberOfSources; sourceNo++) {
        // Determine the fields for the input frame
        firstFieldNumber[sourceNo] = (frameNumber * 2) -1;
        secondFieldNumber[sourceNo] = (frameNumber * 2);
		
        // Fetch the input data (get the fields in TBC sequence order to save seeking)
        if (firstFieldNumber[sourceNo] < secondFieldNumber[sourceNo]) {
            firstFieldVideoData[sourceNo] = sourceVideos[sourceNo]->getVideoField(firstFieldNumber[sourceNo]);
            secondFieldVideoData[sourceNo] = sourceVideos[sourceNo]->getVideoField(secondFieldNumber[sourceNo]);
        } else {
            secondFieldVideoData[sourceNo] = sourceVideos[sourceNo]->getVideoField(secondFieldNumber[sourceNo]);
            firstFieldVideoData[sourceNo] = sourceVideos[sourceNo]->getVideoField(firstFieldNumber[sourceNo]);
        }

        firstFieldMetadata[sourceNo] = ldDecodeMetaData[sourceNo]->getField(firstFieldNumber[sourceNo]);
        secondFieldMetadata[sourceNo] = ldDecodeMetaData[sourceNo]->getField(secondFieldNumber[sourceNo]);
        videoParameters[sourceNo] = ldDecodeMetaData[sourceNo]->getVideoParameters();
    }

    // Figure out which of the available sources can be used to correct the current frame
    availableSourcesForFrame.clear();
    if (numberOfSources > 1) {
        availableSourcesForFrame = getAvailableSourcesForFrame(frameNumber);//getAvailableSourcesForFrame(currentVbiFrame);
    } else {
        availableSourcesForFrame.append(0);
    }

    // Set the other miscellaneous parameters
    _reverse = reverse;
	
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

// Method that returns a vector of the sources that contain data for the required VBI frame number
QVector<qint32> SequencingPool::getAvailableSourcesForFrame(qint32 vbiFrameNumber)
{
    QVector<qint32> availableSourcesForFrame;
    /*for (qint32 sourceNo = 0; sourceNo < sourceVideos.size(); sourceNo++) {
        if (vbiFrameNumber >= sourceMinimumVbiFrame[sourceNo] && vbiFrameNumber <= sourceMaximumVbiFrame[sourceNo]) {
            // Get the field numbers for the frame - THIS CRASHES
            qint32 sequentialFrameNumber = convertVbiFrameNumberToSequential(vbiFrameNumber, sourceNo);

            // Check the source contains enough frames to have the required sequential frame
            if (ldDecodeMetaData[sourceNo]->getNumberOfFrames() < sequentialFrameNumber)
            {
                // Sequential frame is out of bounds
                qDebug() << "VBI Frame number" << vbiFrameNumber << "is out of bounds for source " << sourceNo;
            } else {
                // Sequential frame is in bounds
                qint32 firstFieldNumber = ldDecodeMetaData[sourceNo]->getFirstFieldNumber(sequentialFrameNumber);
                qint32 secondFieldNumber = ldDecodeMetaData[sourceNo]->getSecondFieldNumber(sequentialFrameNumber);

                // Ensure the frame is not a padded field (i.e. missing)
                if (ldDecodeMetaData[sourceNo]->getField(firstFieldNumber).pad == false && ldDecodeMetaData[sourceNo]->getField(secondFieldNumber).pad == false) {
                    availableSourcesForFrame.append(sourceNo);
                } else {
                    if (ldDecodeMetaData[sourceNo]->getField(firstFieldNumber).pad == true) qDebug() << "First field number" << firstFieldNumber << "of source" << sourceNo << "is padded";
                    if (ldDecodeMetaData[sourceNo]->getField(secondFieldNumber).pad == true) qDebug() << "Second field number" << firstFieldNumber << "of source" << sourceNo << "is padded";
                }
            }
        }
    }

    if (availableSourcesForFrame.size() != sourceVideos.size()) {
        if (availableSourcesForFrame.size() > 0) {
            qDebug() << "VBI Frame number" << vbiFrameNumber << "has only" << availableSourcesForFrame.size() << "available sources";
        } else {
            qInfo() << "Warning: VBI Frame number" << vbiFrameNumber << "has ZERO available sources (all sources padded?)";
        }
    }*/
	availableSourcesForFrame.append(0);//return only first source as available for testing
    return availableSourcesForFrame;
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
