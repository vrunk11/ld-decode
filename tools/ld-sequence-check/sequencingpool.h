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

#ifndef SEQUENCINGPOOL_H
#define SEQUENCINGPOOL_H

#include <QObject>
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>

#include "sourcevideo.h"
#include "lddecodemetadata.h"
#include "sequencer.h"

class SequencingPool : public QObject
{
    Q_OBJECT
public:
    explicit SequencingPool(QString _outputFilename, QString _outputJsonFilename,
                           qint32 _maxThreads, QVector<LdDecodeMetaData *> &_ldDecodeMetaData, QVector<SourceVideo *> &_sourceVideos,
                           bool _isCav, long _offset, QObject *parent = nullptr);

    bool process();

    // Member functions used by worker threads
	void getParameters(long& _offset,bool& _isCav);
	
    bool getInputFrame(qint32& frameNumber,
                       QVector<qint32> &firstFieldNumber, QVector<SourceVideo::Data> &firstFieldVideoData, QVector<LdDecodeMetaData::Field> &firstFieldMetadata,
                       QVector<qint32> &secondFieldNumber, QVector<SourceVideo::Data> &secondFieldVideoData, QVector<LdDecodeMetaData::Field> &secondFieldMetadata,
                       QVector<LdDecodeMetaData::VideoParameters> &videoParameters,
                       QVector<qint32> &availableSourcesForFrame);

    bool setOutputFrame(qint32 frameNumber,
                        SourceVideo::Data firstTargetFieldData, SourceVideo::Data secondTargetFieldData,
                        qint32 firstFieldSeqNo, qint32 secondFieldSeqNo);

private:
    QString outputFilename;
    QString outputJsonFilename;
    qint32 maxThreads;
    bool isCav;
    long offset;
    QElapsedTimer totalTimer;

    // Atomic abort flag shared by worker threads; workers watch this, and shut
    // down as soon as possible if it becomes true
    QAtomicInt abort;

    // Input stream information (all guarded by inputMutex while threads are running)
    QMutex inputMutex;
    qint32 inputFrameNumber;
    qint32 lastFrameNumber;
    QVector<LdDecodeMetaData *> &ldDecodeMetaData;
    QVector<SourceVideo *> &sourceVideos;

    // Output stream information (all guarded by outputMutex while threads are running)
    QMutex outputMutex;

    struct OutputFrame {
        SourceVideo::Data firstTargetFieldData;
        SourceVideo::Data secondTargetFieldData;
        qint32 firstFieldSeqNo;
        qint32 secondFieldSeqNo;
    };

    qint32 outputFrameNumber;
    QMap<qint32, OutputFrame> pendingOutputFrames;
    QFile targetVideo;

    // Local source information
    QVector<bool> sourceDiscTypeCav;
    QVector<qint32> sourceMinimumVbiFrame;
    QVector<qint32> sourceMaximumVbiFrame;

    QVector<qint32> getAvailableSourcesForFrame(qint32 vbiFrameNumber);
    bool writeOutputField(const SourceVideo::Data &fieldData);
    LdDecodeMetaData &correctMetaData();
};

#endif // SEQUENCINGPOOL_H
