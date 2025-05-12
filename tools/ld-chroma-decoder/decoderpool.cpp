/************************************************************************

    decoderpool.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018-2019 Simon Inns
    Copyright (C) 2021 Phillip Blucas
    Copyright (C) 2021 Adam Sampson

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

#include "decoderpool.h"

DecoderPool::DecoderPool(Decoder &_videoDecoder, Decoder &_chromaDecoder, QString _inputFileName, QString _chromaFileName,
                         LdDecodeMetaData &_ldDecodeMetaData,
                         OutputWriter::Configuration &_outputConfig, QString _outputFileName,
                         qint32 _startFrame, qint32 _length, qint32 _maxThreads)
    : videoDecoder(_videoDecoder), chromaDecoder(_chromaDecoder), inputFileName(_inputFileName), chromaFileName(_chromaFileName),
      outputConfig(_outputConfig), outputFileName(_outputFileName),
      startFrame(_startFrame), length(_length), maxThreads(_maxThreads),
      abort(false), ldDecodeMetaData(_ldDecodeMetaData)
{
}

Decoder& DecoderPool::getDecoder() { return videoDecoder; }
MonoDecoder& DecoderPool::getDecoderAsMono() { return  static_cast<MonoDecoder&> (videoDecoder); }

bool DecoderPool::process()
{
    videoParameters = ldDecodeMetaData.getVideoParameters();

    // Configure the OutputWriter, adjusting videoParameters
    outputWriter.updateConfiguration(videoParameters, outputConfig);
    outputWriter.printOutputInfo();
	
	// Open the source video file
    if (!sourceVideo.open(inputFileName, videoParameters.fieldWidth * videoParameters.fieldHeight)) {
        // Could not open source video file
        qInfo() << "Unable to open ld-decode video file";
        return false;
    }
	
	// Open the chroma video file if available and set isYC
	if(chromaFileName != "")
	{
		if (!sourceChroma.open(chromaFileName, videoParameters.fieldWidth * videoParameters.fieldHeight)) {
			// Could not open chroma video file
			qInfo() << "Unable to open ld-decode chroma video file";
			return false;
		}
		isYC = true;
	}

    // Configure the decoder, and check that it can accept this video
    if (!videoDecoder.configure(videoParameters)) {
        return false;
    }
	
	// Get the decoder's lookbehind/lookahead requirements
    decoderLookBehind = videoDecoder.getLookBehind();
    decoderLookAhead = videoDecoder.getLookAhead();
	
	if(isYC)
	{
		if (!chromaDecoder.configure(videoParameters)) {
			return false;
		}
		// Get the decoder's lookbehind/lookahead requirements
		decoderLookBehindChroma = chromaDecoder.getLookBehind();
		decoderLookAheadChroma = chromaDecoder.getLookAhead();
	}

    // If no startFrame parameter was specified, set the start frame to 1
    if (startFrame == -1) startFrame = 1;

    if (startFrame > ldDecodeMetaData.getNumberOfFrames()) {
        qInfo() << "Specified start frame is out of bounds, only" << ldDecodeMetaData.getNumberOfFrames() << "frames available";
        return false;
    }

    // If no length parameter was specified set the length to the number of available frames
    if (length == -1) {
        length = ldDecodeMetaData.getNumberOfFrames() - (startFrame - 1);
    } else {
        if (length + (startFrame - 1) > ldDecodeMetaData.getNumberOfFrames()) {
            qInfo() << "Specified length of" << length << "exceeds the number of available frames, setting to" << ldDecodeMetaData.getNumberOfFrames() - (startFrame - 1);
            length = ldDecodeMetaData.getNumberOfFrames() - (startFrame - 1);
        }
    }

    // Open the output file
    if (outputFileName == "-") {
        // No output filename, use stdout instead
        if (!targetVideo.open(stdout, QIODevice::WriteOnly)) {
            // Failed to open stdout
            qCritical() << "Could not open stdout for output";
            sourceVideo.close();
			if(isYC)
			{
				sourceChroma.close();
			}
            return false;
        }
        qInfo() << "Writing output to stdout";
    } else {
        // Open output file
        targetVideo.setFileName(outputFileName);
        if (!targetVideo.open(QIODevice::WriteOnly)) {
            // Failed to open output file
            qCritical() << "Could not open" << outputFileName << "for output";
            sourceVideo.close();
			if(isYC)
			{
				sourceChroma.close();
			}
            return false;
        }
    }
	
	//libav data
	if(outputConfig.outputHeader != "raw" || outputConfig.outputHeader != "y4m")
	{
		//write metadata for header
		QStringList metadataLine;
		if(videoParameters.system == NTSC)//MonoDecoder& DecoderPool::getDecoderAsMono() { return  static_cast<MonoDecoder&> (videoDecoder); }
		{
			Comb::Configuration* decoderCfg = &static_cast<NtscDecoder&>(videoDecoder).getConfig().combConfig;//nullptr;
			if(isYC)
			{
				decoderCfg = &static_cast<NtscDecoder&>(chromaDecoder).getConfig().combConfig;
			}
			
			metadataLine << "Decoded by ld-chroma-decoder"
						<< "\nSource :" << (isYC ? "Y/C" : "CVBS")
						<< "\nStandard :" << (videoParameters.system == PAL ? "PAL" : videoParameters.system == PAL_M ? "PAL_M" : "NTSC")
						<< "\nDecoder :" << QString("NTSC %1D").arg(decoderCfg->dimensions)
						<< "\nChroma gain :" << QString::number(decoderCfg->chromaGain)
						<< "\nChroma phase :" << QString::number(decoderCfg->chromaPhase)
						<< "\nPhase compensation :" << (decoderCfg->phaseCompensation ? "ON" : "OFF")
						<< "\nLuma NR :" << QString::number(decoderCfg->yNRLevel)
						<< "\nChroma NR :" << QString::number(decoderCfg->cNRLevel)
						<< "\nBlack level :" << QString::number(qRound(videoParameters.black16bIre/256.0)) << "/" << QString::number(videoParameters.black16bIre)
						<< "\nWhite level :" << QString::number(qRound(videoParameters.white16bIre/256.0)) << "/" << QString::number(videoParameters.white16bIre)
						<< "\nActive video start :" << QString::number(videoParameters.activeVideoStart)
						<< "\nActive video end :" << QString::number(videoParameters.activeVideoEnd)
						<< "\nGithub : https://github.com/happycube/ld-decode\n";
		}
		else//PAL
		{
			PalColour::Configuration* decoderCfg = &static_cast<PalDecoder&>(videoDecoder).getConfig().pal;
			if(isYC)
			{
				decoderCfg = &static_cast<PalDecoder&>(chromaDecoder).getConfig().pal;
			}

			metadataLine << "Decoded by ld-chroma-decoder"
						<< "\nSource :" << (isYC ? "Y/C" : "CVBS")
						<< "\nStandard :" << (videoParameters.system == PAL ? "PAL" : videoParameters.system == PAL_M ? "PAL_M" : "NTSC")
						<< "\nDecoder :" << (decoderCfg->chromaFilter == PalColour::transform3DFilter ? "PAL Transform 3D" : decoderCfg->chromaFilter == PalColour::transform2DFilter ? "PAL Transform 2D" : "PAL 2D")
						<< "\nTransform threshold :" << QString::number(decoderCfg->transformThreshold)
						<< "\nSimple PAL :" << (decoderCfg->simplePAL ? "ON" : "OFF")
						<< "\nChroma gain :" << QString::number(decoderCfg->chromaGain)
						<< "\nChroma phase :" << QString::number(decoderCfg->chromaPhase)
						<< "\nLuma NR :" << QString::number(decoderCfg->yNRLevel)
						<< "\nBlack level :" << QString::number(qRound(videoParameters.black16bIre/256.0)) << "/" << QString::number(videoParameters.black16bIre)
						<< "\nWhite level :" << QString::number(qRound(videoParameters.white16bIre/256.0)) << "/" << QString::number(videoParameters.white16bIre)
						<< "\nActive video start :" << QString::number(videoParameters.activeVideoStart)
						<< "\nActive video end :" << QString::number(videoParameters.activeVideoEnd)
						<< "\nGithub : https://github.com/happycube/ld-decode\n";
		}
		QString metadataTxt = metadataLine.join(' ');
		outputWriter.initVideoEncoding(fmt_ctx, stream, codec, codec_ctx, codec_opt, frame, outputFileName, metadataTxt);
		pkt = av_packet_alloc();
	}

    // Write the stream header (if there is one)
    const QByteArray streamHeader = outputWriter.getY4mHeader();
    if (streamHeader.size() != 0 && targetVideo.write(streamHeader) == -1) {
        qCritical() << "Writing to the output video file failed";
        return false;
    }

    qInfo() << "Using" << maxThreads << "threads";
    qInfo() << "Processing from start frame #" << startFrame << "with a length of" << length << "frames";

    // Initialise processing state
    inputFrameNumber = startFrame;
    outputFrameNumber = startFrame;
    lastFrameNumber = length + (startFrame - 1);
    totalTimer.start();

    // Start a vector of filtering threads to process the video
    QVector<QThread *> threads;
    threads.resize(maxThreads);
    for (qint32 i = 0; i < maxThreads; i++) {
		if(isYC)
		{
			//videoDecoder contain the chroma decoder
			threads[i] = chromaDecoder.makeThread(abort, *this);
		}
		else
		{
			//videoDecoder contain the cvbs decoder
			threads[i] = videoDecoder.makeThread(abort, *this);
		}
        
        threads[i]->start(QThread::LowPriority);
    }

    // Wait for the workers to finish
    for (qint32 i = 0; i < maxThreads; i++) {
        threads[i]->wait();
        delete threads[i];
    }

    // Did any of the threads abort?
    if (abort) {
        sourceVideo.close();
        targetVideo.close();
		if(outputConfig.outputHeader != "raw" || outputConfig.outputHeader != "y4m")
		{
			av_write_trailer(fmt_ctx);
			avformat_free_context(fmt_ctx);
			av_frame_free(&frame);
			av_packet_free(&pkt);
			av_dict_free(&codec_opt);
			avcodec_free_context(&codec_ctx);
		}
		if(isYC)
		{
			sourceChroma.close();
		}
        return false;
    }

    // Check we've processed all the frames, now the workers have finished
    if (inputFrameNumber != (lastFrameNumber + 1) || outputFrameNumber != (lastFrameNumber + 1)
        || !pendingOutputFrames.empty() || !pendingOutputFrames8.empty()) {
        qCritical() << "Incorrect state at end of processing";
        sourceVideo.close();
        targetVideo.close();
		if(outputConfig.outputHeader != "raw" || outputConfig.outputHeader != "y4m")
		{
			av_write_trailer(fmt_ctx);
			avformat_free_context(fmt_ctx);
			av_frame_free(&frame);
			av_packet_free(&pkt);
			av_dict_free(&codec_opt);
			avcodec_free_context(&codec_ctx);
		}
		if(isYC)
		{
			sourceChroma.close();
		}
        return false;
    }

    double totalSecs = (static_cast<double>(totalTimer.elapsed()) / 1000.0);
    qInfo() << "Processing complete -" << length << "frames in" << totalSecs << "seconds (" <<
               length / totalSecs << "FPS )";

    // Close the source video
    sourceVideo.close();
	
	//finalise file and close
	if(outputConfig.outputHeader == "raw" || outputConfig.outputHeader == "y4m")
	{
		av_write_trailer(fmt_ctx);
		avformat_free_context(fmt_ctx);
		av_frame_free(&frame);
		av_packet_free(&pkt);
		av_dict_free(&codec_opt);
		avcodec_free_context(&codec_ctx);
	}
	
	// Close chroma if available
	if(isYC)
	{
		sourceChroma.close();
	}

    // Close the target video
    targetVideo.close();

    return true;
}

bool DecoderPool::getInputFrames(qint32 &startFrameNumber, QVector<SourceField> &fields, qint32 &startIndex, qint32 &endIndex)
{
    QMutexLocker locker(&inputMutex);

    // Work out a reasonable batch size to provide work for all threads.
    // This assumes that the synchronisation to get a new batch is less
    // expensive than computing a single frame, so a batch size of 1 is
    // reasonable.
    const qint32 maxBatchSize = qMin(DEFAULT_BATCH_SIZE, qMax(1, length / maxThreads));

    // Work out how many frames will be in this batch
    qint32 batchFrames = qMin(maxBatchSize, lastFrameNumber + 1 - inputFrameNumber);
    if (batchFrames == 0) {
        // No more input frames
        return false;
    }

    // Advance the frame number
    startFrameNumber = inputFrameNumber;
    inputFrameNumber += batchFrames;
	
    // Load the fields
    SourceField::loadFields(sourceVideo, ldDecodeMetaData,
                            startFrameNumber, batchFrames, decoderLookBehind, decoderLookAhead,
                            fields, startIndex, endIndex);

    return true;
}

bool DecoderPool::getYCFrames(qint32 &startFrameNumber, QVector<SourceField> &lumaFields, QVector<SourceField> &chromaFields, qint32 &startIndex, qint32 &endIndex)
{
    QMutexLocker locker(&inputMutex);

    // Work out a reasonable batch size to provide work for all threads.
    // This assumes that the synchronisation to get a new batch is less
    // expensive than computing a single frame, so a batch size of 1 is
    // reasonable.
    const qint32 maxBatchSize = qMin(DEFAULT_BATCH_SIZE, qMax(1, length / maxThreads));

    // Work out how many frames will be in this batch
    qint32 batchFrames = qMin(maxBatchSize, lastFrameNumber + 1 - inputFrameNumber);
    if (batchFrames == 0) {
        // No more input frames
        return false;
    }

    // Advance the frame number
    startFrameNumber = inputFrameNumber;
    inputFrameNumber += batchFrames;
	
    // Load the Y fields
    SourceField::loadFields(sourceVideo, ldDecodeMetaData,
                            startFrameNumber, batchFrames, decoderLookBehindChroma, decoderLookAheadChroma,
                            lumaFields, startIndex, endIndex);
	
	// Load the C fields
    SourceField::loadFields(sourceChroma, ldDecodeMetaData,
                            startFrameNumber, batchFrames, decoderLookBehindChroma, decoderLookAheadChroma,
                            chromaFields, startIndex, endIndex);

    return true;
}

bool DecoderPool::putOutputFrames(qint32 startFrameNumber, QVector<OutputFrame> &outputFrames)
{
    QMutexLocker locker(&outputMutex);

    for (qint32 i = 0; i < outputFrames.size(); i++) {
        if (!putOutputFrame(startFrameNumber + i, outputFrames[i])) {
            return false;
        }
    }

    return true;
}

// Write one output frame. You must hold outputMutex to call this.
//
// The worker threads will complete frames in an arbitrary order, so we can't
// just write the frames to the output file directly. Instead, we keep a map of
// frames that haven't yet been written; when a new frame comes in, we check
// whether we can now write some of them out.
//
// Returns true on success, false on failure.
bool DecoderPool::putOutputFrame(qint32 frameNumber, OutputFrame &outputFrame)
{
	if(outputConfig.outputHeader == "raw" || outputConfig.outputHeader == "y4m")
	{
		//convert to 8bit for yuv411p cause there is no 16bit variant
		if(outputConfig.is8bit)
		{
			QVector<quint8> outputFrame8;
			outputFrame8.resize(outputFrame.size());
			for (int i = 0; i < outputFrame.size(); ++i)
			{
				outputFrame8[i] = qRound(outputFrame[i] / 256.0) ;
			}
			// Put this frame into the map
			pendingOutputFrames8[frameNumber] = outputFrame8;
			
			// Write out as many frames as possible
			while (pendingOutputFrames8.contains(outputFrameNumber))
			{
				const QVector<quint8>& outputData8 = pendingOutputFrames8.value(outputFrameNumber);

				// Write the frame header (if there is one)
				const QByteArray frameHeader = outputWriter.getFrameHeader();
				if (frameHeader.size() != 0 && targetVideo.write(frameHeader) == -1) {
					qCritical() << "Writing to the output video file failed";
					return false;
				}
				
				// Write the frame data
				if (targetVideo.write(reinterpret_cast<const char *>(outputData8.data()), outputData8.size()) == -1) {
					qCritical() << "Writing to the output video file failed";
					return false;
				}

				pendingOutputFrames8.remove(outputFrameNumber);
				outputFrameNumber++;

				const qint32 outputCount = outputFrameNumber - startFrame;
				if ((outputCount % 32) == 0) {
					// Show an update to the user
					double fps = outputCount / (static_cast<double>(totalTimer.elapsed()) / 1000.0);
					qInfo() << outputCount << "frames processed -" << fps << "FPS";
				}
			}
		}
		else
		{
			// Put this frame into the map
			pendingOutputFrames[frameNumber] = outputFrame;
			
			// Write out as many frames as possible
			while (pendingOutputFrames.contains(outputFrameNumber)) 
			{
				const OutputFrame& outputData = pendingOutputFrames.value(outputFrameNumber);

				// Write the frame header (if there is one)
				const QByteArray frameHeader = outputWriter.getFrameHeader();
				if (frameHeader.size() != 0 && targetVideo.write(frameHeader) == -1) {
					qCritical() << "Writing to the output video file failed";
					return false;
				}
				
				// Write the frame data
				if (targetVideo.write(reinterpret_cast<const char *>(outputData.data()), outputData.size() * 2) == -1) {
					qCritical() << "Writing to the output video file failed";
					return false;
				}

				pendingOutputFrames.remove(outputFrameNumber);
				outputFrameNumber++;

				const qint32 outputCount = outputFrameNumber - startFrame;
				if ((outputCount % 32) == 0) {
					// Show an update to the user
					double fps = outputCount / (static_cast<double>(totalTimer.elapsed()) / 1000.0);
					qInfo() << outputCount << "frames processed -" << fps << "FPS";
				}
			}
		}
	}
	else//use libav
	{
		qint32 outputHeight = outputWriter.getOutputHeight();
		qint32 outputWidth = outputWriter.getOutputWidth();
		
		// enqueue the completed frame
        pendingOutputFrames[frameNumber] = outputFrame;

        // drain in-order
        while (pendingOutputFrames.contains(outputFrameNumber)) {
            OutputFrame& outputData = pendingOutputFrames[outputFrameNumber];
			
			if (av_frame_make_writable(frame) < 0) {
				qFatal("Could not make frame writable");
				return false;
			}
			
			switch (outputConfig.pixelFormat) {
				case OutputWriter::PixelFormat::RGB:
				{
					uint16_t* srcData = (uint16_t*)outputData.data();
					int pixelSize = 3; // RGB has 3 components
					
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + y * outputWidth * pixelSize;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[0] + y * frame->linesize[0]);
							// 8bit convertion
							for (int x = 0; x < outputWidth; x++) {
								uint16_t r16 = src[3 * x + 0];
								uint16_t g16 = src[3 * x + 1];
								uint16_t b16 = src[3 * x + 2];

								// Convert 16-bit to 8-bit by dividing by 256
								uint8_t r8 = qBound(0, qRound(r16 / 256.0), 255);
								uint8_t g8 = qBound(0, qRound(g16 / 256.0), 255);
								uint8_t b8 = qBound(0, qRound(b16 / 256.0), 255);

								// Assign to dst in BGR0 order
								dst[4 * x + 0] = b8;     // Blue
								dst[4 * x + 1] = g8;     // Green
								dst[4 * x + 2] = r8;     // Red
								dst[4 * x + 3] = 0x00;   // Unused alpha byte
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*)(frame->data[0] + y * frame->linesize[0]);
							memcpy(dst, src, outputWidth * pixelSize * sizeof(uint16_t));
						}
					}
					break;
				}
				case OutputWriter::PixelFormat::YUV444:
				{
					uint16_t* srcData = (uint16_t*)outputData.data();
					int yPlaneOffset = 0;
					int uPlaneOffset = outputWidth * outputHeight;
					int vPlaneOffset = uPlaneOffset + outputWidth * outputHeight;
					
					// Copy Y plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + yPlaneOffset + y * outputWidth;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[0] + y * frame->linesize[0]);
							// 8bit convertion
							for (int x = 0; x < outputWidth; x++) {
								dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*)(frame->data[0] + y * frame->linesize[0]);
							memcpy(dst, src, outputWidth * sizeof(uint16_t));
						}
					}
					
					// Copy U plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + uPlaneOffset + y * outputWidth;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[1] + y * frame->linesize[1]);
							// 8bit convertion
							for (int x = 0; x < outputWidth; x++) {
								dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*)(frame->data[1] + y * frame->linesize[1]);
							memcpy(dst, src, outputWidth * sizeof(uint16_t));
						}
					}
					
					// Copy V plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + vPlaneOffset + y * outputWidth;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[2] + y * frame->linesize[2]);
							// 8bit convertion
							for (int x = 0; x < outputWidth; x++) {
								dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*)(frame->data[2] + y * frame->linesize[2]);
							memcpy(dst, src, outputWidth * sizeof(uint16_t));
						}
					}
					break;
				}
				case OutputWriter::PixelFormat::YUV422:
				{
					uint16_t* srcData = (uint16_t*)outputData.data();
					int yPlaneSize = outputWidth * outputHeight;
					int chromaWidth = outputWidth / 2;
					int uPlaneOffset = yPlaneSize;
					int vPlaneOffset = uPlaneOffset + (chromaWidth * outputHeight);
					
					// Copy Y plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + y * outputWidth;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[0] + y * frame->linesize[0]);
							// 8bit convertion
							for (int x = 0; x < outputWidth; x++) {
								dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*) (frame->data[0] + y * frame->linesize[0]);
							memcpy(dst, src, outputWidth * sizeof(uint16_t));
						}
					}
					
					// Copy U plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + uPlaneOffset + y * chromaWidth;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[1] + y * frame->linesize[1]);
							// 8bit convertion
							for (int x = 0; x < chromaWidth; x++) {
								dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*) (frame->data[1] + y * frame->linesize[1]);
							memcpy(dst, src, chromaWidth * sizeof(uint16_t));
						}
					}
					
					// Copy V plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + vPlaneOffset + y * chromaWidth;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[2] + y * frame->linesize[2]);
							// 8bit convertion
							for (int x = 0; x < chromaWidth; x++) {
								dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*) (frame->data[2] + y * frame->linesize[2]);
							memcpy(dst, src, chromaWidth * sizeof(uint16_t));
						}
					}
					break;
				}
				case OutputWriter::PixelFormat::YUV411:
				{
					uint16_t* srcData = (uint16_t*)outputData.data();
					int yPlaneSize = outputWidth * outputHeight;
					int chromaWidth = outputWidth / 4;
					int uPlaneOffset = yPlaneSize;
					int vPlaneOffset = uPlaneOffset + (chromaWidth * outputHeight);
					
					// Copy Y plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint8_t* dst = frame->data[0] + y * frame->linesize[0];
						uint16_t* src = srcData + y * outputWidth;
						// 8bit convertion
						for (int x = 0; x < outputWidth; x++) {
							dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
						}
					}
					
					// Copy U plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint8_t* dst = frame->data[1] + y * frame->linesize[1];
						uint16_t* src = srcData + uPlaneOffset + y * chromaWidth;
						// 8bit convertion
						for (int x = 0; x < chromaWidth; x++) {
							dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
						}
					}
					
					// Copy V plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint8_t* dst = frame->data[2] + y * frame->linesize[2];
						uint16_t* src = srcData + vPlaneOffset + y * chromaWidth;
						// 8bit convertion
						for (int x = 0; x < chromaWidth; x++) {
							dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
						}
					}
					break;
				}
				case OutputWriter::PixelFormat::GRAY:
				{
					uint16_t* srcData = (uint16_t*)outputData.data();
					int yPlaneOffset = 0;
					
					// Copy Y plane line by line
					for (int y = 0; y < outputHeight; y++) {
						uint16_t* src = srcData + yPlaneOffset + y * outputWidth;
						if(outputConfig.is8bit)
						{
							uint8_t* dst = (frame->data[0] + y * frame->linesize[0]);
							// 8bit convertion
							for (int x = 0; x < outputWidth; x++) {
								dst[x] = static_cast<uint8_t>(round(src[x] / 256.0));
							}
						}
						else
						{
							uint16_t* dst = (uint16_t*)(frame->data[0] + y * frame->linesize[0]);
							memcpy(dst, src, outputWidth * sizeof(uint16_t));
						}
					}
					break;
				}
			}
			//memcpy(frame->data[0], outputData.data(), outputData.size() * sizeof(uint16_t));
			//qInfo() << frame->linesize[0];
			
			frame->pts = outputFrameNumber;
			
			// Send frame to encoder
			int ret = avcodec_send_frame(codec_ctx, frame);
			if (ret < 0) {
				qFatal("Error sending frame to encoder");
				return false;
			}
			
			// Receive encoded packets
			AVPacket* pkt = av_packet_alloc();
			while (ret >= 0) {
				ret = avcodec_receive_packet(codec_ctx, pkt);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				} else if (ret < 0) {
					qFatal("Error receiving packet from encoder");
					av_packet_free(&pkt);
					return false;
				}
				
				// Rescale timestamps
				av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
				pkt->stream_index = stream->index;
				
				// Write packet
				if (av_interleaved_write_frame(fmt_ctx, pkt) < 0) {
					qFatal("Error muxing packet");
					av_packet_free(&pkt);
					return false;
				}
			}

            pendingOutputFrames.remove(outputFrameNumber);
            outputFrameNumber++;

            // progress logging
			const qint32 outputCount = outputFrameNumber - startFrame;
			if ((outputCount % 32) == 0) {
				// Show an update to the user
				double fps = outputCount / (static_cast<double>(totalTimer.elapsed()) / 1000.0);
				qInfo() << outputCount << "frames processed -" << fps << "FPS";
			}
        }
    }

    return true;
}
