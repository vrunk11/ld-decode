/************************************************************************

    outputwriter.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018-2021 Chad Page
    Copyright (C) 2020-2021 Adam Sampson
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

#include "outputwriter.h"

#include "componentframe.h"

// Limits, zero points and scaling factors (from 0-1) for Y'CbCr colour representations
// [Poynton ch25 p305] [BT.601-7 sec 2.5.3]
static constexpr double Y_MIN   = 1.0    * 256.0;
static constexpr double Y_ZERO  = 16.0   * 256.0;
static constexpr double Y_SCALE = 219.0  * 256.0;
static constexpr double Y_MAX   = 254.75 * 256.0;
static constexpr double C_MIN   = 1.0    * 256.0;
static constexpr double C_ZERO  = 128.0  * 256.0;
static constexpr double C_SCALE = 112.0  * 256.0;
static constexpr double C_MAX   = 254.75 * 256.0;

// ITU-R BT.601-7
// [Poynton eq 25.1 p303 and eq 25.5 p307]
static constexpr double ONE_MINUS_Kb = 1.0 - 0.114;
static constexpr double ONE_MINUS_Kr = 1.0 - 0.299;

// kB = sqrt(209556997.0 / 96146491.0) / 3.0
// kR = sqrt(221990474.0 / 288439473.0)
// [Poynton eq 28.1 p336]
static constexpr double kB = 0.49211104112248356308804691718185;
static constexpr double kR = 0.87728321993817866838972487283129;

void OutputWriter::updateConfiguration(LdDecodeMetaData::VideoParameters &_videoParameters,
                                       const OutputWriter::Configuration &_config)
{
    config = _config;
    videoParameters = _videoParameters;
    topPadLines = 0;
    bottomPadLines = 0;

    activeWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    activeHeight = videoParameters.lastActiveFrameLine - videoParameters.firstActiveFrameLine;
    outputHeight = activeHeight;
	outputWidth = config.resampleWidth;
	
	qint32 outWidth = config.useResampling ? config.resampleWidth : videoParameters.activeVideoEnd - videoParameters.activeVideoStart;

    if (config.paddingAmount > 1) {
        // Some video codecs require the width and height of a video to be divisible by
        // a given number of samples on each axis.
        
        // Expand horizontal active region so the width is divisible by the specified padding factor.
        while (true) {
			activeWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
            if ((activeWidth % config.paddingAmount) == 0) {
                break;
            }

            // Add pixels to the right and left sides in turn, to keep the active area centred
            if ((activeWidth % 2) == 0) {
                videoParameters.activeVideoEnd++;
            } else {
                videoParameters.activeVideoStart--;
            }
        }

        // Insert empty padding lines so the height is divisible by by the specified padding factor.
        while (true) {
            outputHeight = topPadLines + activeHeight + bottomPadLines;
            if ((outputHeight % config.paddingAmount) == 0) {
                break;
            }

            // Add lines to the bottom and top in turn, to keep the active area centred
            if ((outputHeight % 2) == 0) {
                bottomPadLines++;
            } else {
                topPadLines++;
            }
        }

        // Update the caller's copy, now we've adjusted the active area
        _videoParameters = videoParameters;
    }
}

const char *OutputWriter::getPixelName() const
{
    switch (config.pixelFormat) {
    case RGB48:
        return "RGB48";
    case YUV444P16:
        return "YUV444P16";
    case GRAY16:
        return "GRAY16";
    default:
        return "unknown";
    }
}

void OutputWriter::printOutputInfo() const
{
    // Show output information to the user
    const qint32 frameHeight = (videoParameters.fieldHeight * 2) - 1;
    qInfo() << "Input video of" << videoParameters.fieldWidth << "x" << frameHeight
            << "will be colourised and trimmed to" << outputWidth << "x" << outputHeight
            << getPixelName() << "frames";
}

QByteArray OutputWriter::getStreamHeader() const
{
    // Only yuv4mpeg output needs a header
    if (!config.outputY4m) {
        return QByteArray();
    }

    QString header;
    QTextStream str(&header);

    str << "YUV4MPEG2";

    // Frame size
    str << " W" << config.resampleWidth;
    str << " H" << outputHeight;

    // Frame rate
    if (videoParameters.system == PAL) {
        str << " F25:1";
    } else {
        str << " F30000:1001";
    }

    // Field order
    if (videoParameters.firstActiveFrameLine % 2 ^ topPadLines % 2) {
        str << " Ib";
    } else {
        str << " It";
    }

    // Pixel aspect ratio
    // Follows EBU R92 and SMPTE RP 187 except that values are scaled from
    // BT.601 sampling (13.5 MHz) to 4fSC
	if(config.useResampling)
	{
		const int height = (videoParameters.system == PAL ? 576 : 480);
		if (videoParameters.isWidescreen) {
			// widescreen DAR = 16:9
			{
				auto num = 16 * height;
				auto den =  9 * config.resampleWidth;
				auto g   = std::gcd(num, den);
				str << " A" << (num/g) << ":" << (den/g);
			}
		} else {
			// standard DAR = 4:3
			{
				auto num =  4 * height;
				auto den =  3 * config.resampleWidth;
				auto g   = std::gcd(num, den);
				str << " A" << (num/g) << ":" << (den/g);
			}
		}
	}
	else
	{
		if (videoParameters.system == PAL) {
			if (videoParameters.isWidescreen) {
				str << " A865:779"; // (16 / 9) * (576 / (702 * 4*fSC / 13.5))
			} else {
				str << " A259:311"; // (4 / 3) * (576 / (702 * 4*fSC / 13.5))
			}
		} else {
			if (videoParameters.isWidescreen) {
				str << " A25:22"; // (16 / 9) * (480 / (708 * 4*fSC / 13.5))
			} else {
				str << " A352:413"; // (4 / 3) * (480 / (708 * 4*fSC / 13.5))
			}
		}
	}

    // Pixel format
    switch (config.pixelFormat) {
    case YUV444P16:
        str << " C444p16 XCOLORRANGE=LIMITED";
        break;
    case GRAY16:
        str << " Cmono16 XCOLORRANGE=LIMITED";
        break;
    default:
        qFatal("pixel format not supported in yuv4mpeg header");
        break;
    }

    str << "\n";
    return header.toUtf8();
}

QByteArray OutputWriter::getFrameHeader() const
{
    // Only yuv4mpeg output needs a header
    if (!config.outputY4m) {
        return QByteArray();
    }

    return QStringLiteral("FRAME\n").toUtf8();
}

void OutputWriter::convert(const ComponentFrame &componentFrame, OutputFrame &outputFrame) const
{
    // Work out the number of output values, and resize the vector accordingly
    qint32 totalSize = outputWidth * outputHeight;
	
    switch (config.pixelFormat) {
    case RGB48:
    case YUV444P16:
        totalSize *= 3;
        break;
    case GRAY16:
        break;
    }
    outputFrame.resize(totalSize);

    // Clear padding
    clearPadLines(0, topPadLines, outputFrame);
    clearPadLines(outputHeight - bottomPadLines, bottomPadLines, outputFrame);

    // Convert active lines
    for (qint32 y = 0; y < activeHeight; y++) {
        convertLine(y, componentFrame, outputFrame);
    }
}

void OutputWriter::clearPadLines(qint32 firstLine, qint32 numLines, OutputFrame &outputFrame) const
{
    switch (config.pixelFormat) {
        case RGB48: {
            // Fill with RGB black
            quint16 *out = outputFrame.data() + (outputWidth * firstLine * 3);

            for (qint32 i = 0; i < numLines * outputWidth * 3; i++) {
                out[i] = 0;
            }

            break;
        }
        case YUV444P16: {
            // Fill Y with black, no chroma
            quint16 *outY  = outputFrame.data() + (outputWidth * firstLine);
            quint16 *outCB = outY + (outputWidth * outputHeight);
            quint16 *outCR = outCB + (outputWidth * outputHeight);

            for (qint32 i = 0; i < numLines * outputWidth; i++) {
                outY[i]  = static_cast<quint16>(Y_ZERO);
                outCB[i] = static_cast<quint16>(C_ZERO);
                outCR[i] = static_cast<quint16>(C_ZERO);
            }

            break;
        }
        case GRAY16: {
            // Fill with black
            quint16 *out = outputFrame.data() + (outputWidth * firstLine);

            for (qint32 i = 0; i < numLines * outputWidth; i++) {
                out[i] = static_cast<quint16>(Y_ZERO);
            }

            break;
        }
    }
}

void OutputWriter::convertLine(qint32 lineNumber, const ComponentFrame &componentFrame, OutputFrame &outputFrame) const
{
    // Get pointers to the component data for the active region
    const qint32 inputLine = videoParameters.firstActiveFrameLine + lineNumber;
    const double *inY = componentFrame.y(inputLine) + videoParameters.activeVideoStart;
    // Not used if output is GRAY16
    const double *inU = (config.pixelFormat != GRAY16)
                         ? componentFrame.u(inputLine) + videoParameters.activeVideoStart
                         : nullptr;
    const double *inV = (config.pixelFormat != GRAY16)
                         ? componentFrame.v(inputLine) + videoParameters.activeVideoStart
                         : nullptr;

    const qint32 outputLine = topPadLines + lineNumber;

    const double yOffset = videoParameters.black16bIre;
    double yRange = videoParameters.white16bIre - videoParameters.black16bIre;
    const double uvRange = yRange;
	
	const qint32 inWidth  = activeWidth;
	
	//qInfo() << inWidth << " / " << config.resampleWidth;
	
	std::vector<double> channelY, channelU, channelV;
	
	// Allocate output buffers
	channelY.resize(outputWidth);
	channelU.resize(outputWidth);
	channelV.resize(outputWidth);
	
	if(config.useResampling)
	{
			size_t flushDone = 0;
			size_t idone = 0, odone = 0;
			soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT64_I, SOXR_FLOAT64_I);
			soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_HQ, 0);
			soxr_runtime_spec_t const runtime_spec = soxr_runtime_spec(1);

			// Create a soxr instance (1 channel, double→double) with default specs
			soxr_error_t errY;
			soxr_t soxrY = soxr_create(inWidth, outputWidth, 1, &errY, &io_spec, &q_spec, &runtime_spec);

			// Resample Y plane
			errY = soxr_process(soxrY, inY, inWidth, &idone, channelY.data(), outputWidth, &odone);
			
			//flush resampled data
			soxr_process(soxrY, nullptr, 0, nullptr, channelY.data(), soxr_delay(soxrY), nullptr);
			
			// Clean up
			soxr_delete(soxrY);
			
			if(config.pixelFormat != GRAY16)
			{
				soxr_error_t errU;
				soxr_error_t errV;
				
				//create resampler
				soxr_t soxrU = soxr_create(inWidth, outputWidth, 1, &errU, &io_spec, &q_spec, &runtime_spec);
				soxr_t soxrV = soxr_create(inWidth, outputWidth, 1, &errV, &io_spec, &q_spec, &runtime_spec);
				
				//resample
				errU = soxr_process(soxrU, inU, inWidth, &idone, channelU.data(), outputWidth, &odone);
				errV = soxr_process(soxrV, inV, inWidth, &idone, channelV.data(), outputWidth, &odone);
				
				//flush resampled data
				soxr_process(soxrU, nullptr, 0, nullptr, channelU.data(), soxr_delay(soxrU), nullptr);
				soxr_process(soxrV, nullptr, 0, nullptr, channelV.data(), soxr_delay(soxrV), nullptr);
				
				// Clean up
				soxr_delete(soxrU);
				soxr_delete(soxrV);
			}
	}
	else
	{
		channelY.assign(inY, inY + inWidth);
		channelU.assign(inU, inU + inWidth);
		channelV.assign(inV, inV + inWidth);
	}
	
    switch (config.pixelFormat) {
        case RGB48: {
            // Convert Y'UV to full-range R'G'B' [Poynton eq 28.6 p337]
            quint16 *out = outputFrame.data() + (outputWidth * outputLine * 3);

            const double yScale = 65535.0 / yRange;
            const double uvScale = 65535.0 / uvRange;

            for (qint32 x = 0; x < outputWidth; x++) {
                // Scale Y'UV to 0-65535
                const double rY = qBound(0.0, (channelY[x] - yOffset) * yScale, 65535.0);
                const double rU = channelU[x] * uvScale;
                const double rV = channelV[x] * uvScale;

                // Convert Y'UV to R'G'B'
                const qint32 pos = x * 3;
                out[pos]     = static_cast<quint16>(qBound(0.0, rY                    + (1.139883 * rV),  65535.0));
                out[pos + 1] = static_cast<quint16>(qBound(0.0, rY + (-0.394642 * rU) + (-0.580622 * rV), 65535.0));
                out[pos + 2] = static_cast<quint16>(qBound(0.0, rY + (2.032062 * rU),                     65535.0));
            }

            break;
        }
        case YUV444P16: {
            // Convert Y'UV to Y'CbCr [Poynton eq 25.5 p307]
            quint16 *outY  = outputFrame.data() + (outputWidth * outputLine);
            quint16 *outCB = outY + (outputWidth * outputHeight);
            quint16 *outCR = outCB + (outputWidth * outputHeight);

            const double yScale = Y_SCALE / yRange;
            const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
            const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;

            for (qint32 x = 0; x < outputWidth; x++) {
                outY[x]  = static_cast<quint16>(qBound(Y_MIN, ((channelY[x] - yOffset) * yScale)  + Y_ZERO, Y_MAX));
                outCB[x] = static_cast<quint16>(qBound(C_MIN, (channelU[x]             * cbScale) + C_ZERO, C_MAX));
                outCR[x] = static_cast<quint16>(qBound(C_MIN, (channelV[x]             * crScale) + C_ZERO, C_MAX));
            }

            break;
        }
        case GRAY16: {
            // Throw away UV and just convert Y' to the same scale as Y'CbCr
            quint16 *out = outputFrame.data() + (outputWidth * outputLine);

            const double yScale = Y_SCALE / yRange;

            for (qint32 x = 0; x < outputWidth; x++) {
                out[x] = static_cast<quint16>(qBound(Y_MIN, ((channelY[x] - yOffset) * yScale) + Y_ZERO, Y_MAX));
            }

            break;
        }
    }
}
