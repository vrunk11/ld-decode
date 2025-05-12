/************************************************************************

    outputwriter.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019-2021 Adam Sampson
    Copyright (C) 2021 Phillip Blucas

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

#ifndef OUTPUTWRITER_H
#define OUTPUTWRITER_H

#include <QtGlobal>
#include <QByteArray>
#include <QVector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <soxr.h>

#include "lddecodemetadata.h"

class ComponentFrame;

// A frame (two interlaced fields), converted to one of the supported output formats.
// Since all the formats currently supported use 16-bit samples, this is just a
// vector of 16-bit numbers.
using OutputFrame = QVector<quint16>;

class OutputWriter {
public:
    // Output pixel formats
    enum PixelFormat {
        RGB = 0,
        YUV444,
        YUV422,
        YUV411,
        GRAY
    };

    // Output settings
    struct Configuration {
        qint32 paddingAmount = 8;
        PixelFormat pixelFormat = RGB;
        bool useOutputHeader = false;
        QString outputHeader = "raw";
        bool useResampling = false;
		qint32 resampleWidth = 720;
    };

    // Set the output configuration, and adjust the VideoParameters to suit.
    // (If usePadding is disabled, this will not change the VideoParameters.)
    void updateConfiguration(LdDecodeMetaData::VideoParameters &videoParameters, const Configuration &config);

    // Print a qInfo message about the output format
    void printOutputInfo() const;

    // Get the header data to be written at the start of the stream
    QByteArray getY4mHeader(bool is8bit) const;
    void initVideoEncoding(AVFormatContext* &fmt_ctx, AVStream* &stream,const AVCodec* &codec, AVCodecContext* &codec_ctx, AVDictionary* &codec_opt, AVFrame* &frame, bool is8bit, QString outputFileName, QString metadataTxt);

    // Get the header data to be written before each frame
    QByteArray getFrameHeader() const;

    // For worker threads: convert a component frame to the configured output format
    void convert(ComponentFrame &componentFrameIn, OutputFrame &outputFrame) const;

    PixelFormat getPixelFormat() const {
        return config.pixelFormat;
    }
	
	qint32 getOutputHeight() const {
        return outputHeight;
    }
	
	qint32 getOutputWidth() const {
        return outputWidth;
    }

private:
    // Configuration parameters
    Configuration config;
    LdDecodeMetaData::VideoParameters videoParameters;

    // Number of blank lines to add at the top and bottom of the output
    qint32 topPadLines;
    qint32 bottomPadLines;

    // Output size
    qint32 activeWidth;
    qint32 activeHeight;
    qint32 outputHeight;
	qint32 outputWidth;

    // Get a string representing the pixel format
    const char *getPixelName() const;

    // Clear padding lines
    void clearPadLines(qint32 firstLine, qint32 numLines, OutputFrame &outputFrame) const;

    // Convert one line
    void convertLine(qint32 lineNumber, const ComponentFrame &componentFrame, OutputFrame &outputFrame) const;
};

#endif // OUTPUTWRITER_H
