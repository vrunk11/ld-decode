/************************************************************************

    sequencer.h

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

#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <QObject>
#include <QElapsedTimer>
#include <QAtomicInt>
#include <QThread>
#include <QDebug>

#include "sourcevideo.h"
#include "lddecodemetadata.h"

class SequencingPool;

class Sequencer : public QThread
{
    Q_OBJECT
public:
    explicit Sequencer(int _idThread,QVector<qint32>& _threadOk, int _maxThreads,QAtomicInt& _abort, SequencingPool& _sequencingPool, QObject *parent = nullptr);
	static qint32 decode24BitOldClv(LdDecodeMetaData::Vbi vbi, bool isPal);

protected:
	//data used for 24 bit manchester encoding
	struct VbiData {
        bool dataL16[24];
        bool dataL16F2[24];
        bool dataL17[24];
        bool dataL17F2[24];
        bool dataL18[24];
        bool dataL18F2[24];
        long vbiNumber[5];//number for each frame
    };
    void run() override;
	void sequenceCheck(long frameNumber, long oldClvStart, long oldClvLatestTime, bool isPal, bool noPhase, QVector<QVector<qint32>> sequenceFieldSeqNo, QVector<QVector<SourceVideo::Data>> sequenceSourceField, QVector<QVector<LdDecodeMetaData::Field>> sequenceFieldMetadata, VbiData* vbiData, QVector<LdDecodeMetaData::VideoParameters>& videoParameters);
	int generate24BitCode(VbiData* vbiData,long frameNumber,bool isCav,bool isPal);
	void encode24BitManchester(QVector<SourceVideo::Data> &fieldData,VbiData *bitCode,bool isCav, const LdDecodeMetaData::VideoParameters& videoParameters);
	int getPhaseId(QVector<SourceVideo::Data> sequenceSourceField, int isPal, LdDecodeMetaData::VideoParameters& videoParameters);
	int mesurePhaseOffset(bool isPal, int phaseId,int phaseId2);
	void blankVbi(QVector<SourceVideo::Data>& fieldData, int isPal, bool isOldClv, LdDecodeMetaData::VideoParameters& videoParameters);

private:
    // Sequencing pool
	int idThread;
	QVector<qint32>& threadOk;
	int maxThreads;
    QAtomicInt& abort;
    SequencingPool& sequencingPool;
    QVector<LdDecodeMetaData::VideoParameters> videoParameters;
};

#endif // SEQUENCER_H
