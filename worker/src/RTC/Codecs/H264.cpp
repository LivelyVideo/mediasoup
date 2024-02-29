#define MS_CLASS "RTC::Codecs::H264"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Codecs/H264.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

namespace RTC
{
	namespace Codecs
	{
	/**********************************************************************
	 *                Added by Amir Pauker 02/27/2024 RND-568
	 * this code is based on ngx_rtmp_bitop.c and ngx_rtmp_codec_module.c
	 *********************************************************************/

	uint64_t H264SPSParser::Read(uint64_t n)
	{
	    uint64_t    v;
	    uint64_t    d;

	    v = 0;

	    while (n) {
	        if (this->pos >= this->last) {
	            this->err = 1;
	            return 0;
	        }

	        d = (this->offs + n > 8 ? (uint64_t) (8 - this->offs) : n);

	        v <<= d;
	        v += (*this->pos >> (8 - this->offs - d)) & ((u_char) 0xff >> (8 - d));

	        this->offs += d;
	        n -= d;

	        if (this->offs == 8) {
	            this->pos++;
	            this->offs = 0;
	        }
	    }

	    return v;
	}


	uint64_t H264SPSParser::ReadGolomb()
	{
	    uint64_t  n;

	    for (n = 0; Read(1) == 0 && !err; n++);

	    return ((uint64_t) 1 << n) + Read(n) - 1;
	}


	void H264SPSParser::ParseSPS(uint16_t &widthOut, uint16_t &heightOut)
	{
	    uint64_t   profile_idc, width, height, crop_left, crop_right,
	               crop_top, crop_bottom, frame_mbs_only, n, cf_idc,
	               num_ref_frames;

	    // profile idc
	    profile_idc = (uint64_t) Read(8);

	    // flags
	    Read(8);

	    // level idc
	    Read(8);

	    /* SPS id */
	    ReadGolomb();

	    if (profile_idc == 100 || profile_idc == 110 ||
	        profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
	        profile_idc == 83 || profile_idc == 86 || profile_idc == 118)
	    {
	        /* chroma format idc */
	        cf_idc = ReadGolomb();

	        if (cf_idc == 3) {

	            /* separate color plane */
	            Read(1);
	        }

	        /* bit depth luma - 8 */
	        ReadGolomb();

	        /* bit depth chroma - 8 */
	        ReadGolomb();

	        /* qpprime y zero transform bypass */
	        Read(1);

	        /* seq scaling matrix present */
	        if (Read(1)) {

	            for (n = 0; n < (cf_idc != 3 ? 8u : 12u); n++) {

	                /* seq scaling list present */
	                if (Read(1)) {

	                    /* TODO: scaling_list()
	                    if (n < 6) {
	                    } else {
	                    }
	                    */
	                }
	            }
	        }
	    }

	    /* log2 max frame num */
	    ReadGolomb();

	    /* pic order cnt type */
	    switch (ReadGolomb()) {
	    case 0:

	        /* max pic order cnt */
	        ReadGolomb();
	        break;

	    case 1:

	        /* delta pic order alwys zero */
	        Read(1);

	        /* offset for non-ref pic */
	        ReadGolomb();

	        /* offset for top to bottom field */
	        ReadGolomb();

	        /* num ref frames in pic order */
	        num_ref_frames = ReadGolomb();

	        for (n = 0; n < num_ref_frames; n++) {

	            /* offset for ref frame */
	            ReadGolomb();
	        }
	    }

	    /* num ref frames */
	    ReadGolomb();

	    /* gaps in frame num allowed */
	    Read(1);

	    /* pic width in mbs - 1 */
	    width = ReadGolomb();

	    /* pic height in map units - 1 */
	    height = ReadGolomb();

	    /* frame mbs only flag */
	    frame_mbs_only = Read(1);

	    if (!frame_mbs_only) {

	        /* mbs adaprive frame field */
	        Read(1);
	    }

	    /* direct 8x8 inference flag */
	    Read(1);

	    /* frame cropping */
	    if (Read(1)) {

	        crop_left   = ReadGolomb();
	        crop_right  = ReadGolomb();
	        crop_top    = ReadGolomb();
	        crop_bottom = ReadGolomb();

	    } else {

	        crop_left = 0;
	        crop_right = 0;
	        crop_top = 0;
	        crop_bottom = 0;
	    }

	    if (!err) {
	        widthOut = (width + 1) * 16 - (crop_left + crop_right) * 2;
	        heightOut = (2 - frame_mbs_only) * (height + 1) * 16 -
	                      (crop_top + crop_bottom) * 2;
	    }
	}

	    /**********************************************************************
        **********************************************************************/

		/* Class methods. */

		H264::PayloadDescriptor* H264::Parse(
		  const uint8_t* data, size_t len, RTC::RtpPacket::FrameMarking* frameMarking, uint8_t frameMarkingLen)
		{
			MS_TRACE();

			if (len < 2)
			{
				return nullptr;
			}

			std::unique_ptr<PayloadDescriptor> payloadDescriptor(new PayloadDescriptor());

			// Use frame-marking.
			if (frameMarking)
			{
				// Read fields.
				payloadDescriptor->s   = frameMarking->start;
				payloadDescriptor->e   = frameMarking->end;
				payloadDescriptor->i   = frameMarking->independent;
				payloadDescriptor->d   = frameMarking->discardable;
				payloadDescriptor->b   = frameMarking->base;
				payloadDescriptor->tid = frameMarking->tid;

				payloadDescriptor->hasTid = true;

				if (frameMarkingLen >= 2)
				{
					payloadDescriptor->hasLid = true;
					payloadDescriptor->lid    = frameMarking->lid;
				}

				if (frameMarkingLen == 3)
				{
					payloadDescriptor->hasTl0picidx = true;
					payloadDescriptor->tl0picidx    = frameMarking->tl0picidx;
				}

				// Detect key frame.
				if (frameMarking->start && frameMarking->independent)
				{
					payloadDescriptor->isKeyFrame = true;
				}
			}

			// NOTE: Unfortunately libwebrtc produces wrong Frame-Marking (without i=1 in
			// keyframes) when it uses H264 hardware encoder (at least in Mac):
			//   https://bugs.chromium.org/p/webrtc/issues/detail?id=10746
			//
			// As a temporal workaround, always do payload parsing to detect keyframes if
			// there is no frame-marking or if there is but keyframe was not detected above.

			// Modified by Amir Pauker 02/27/2024 RND-568
			//original: if (!frameMarking || !payloadDescriptor->isKeyFrame)
			if (true)
			{
				const uint8_t nal = *data & 0x1F;

				switch (nal)
				{
					// Single NAL unit packet.
					// IDR (instantaneous decoding picture).
					case 7:
					{
						payloadDescriptor->isKeyFrame = true;

		                // Added by Amir Pauker 02/27/2024 RND-568
		                H264SPSParser spsParser(data + 1, data + len);
		                spsParser.ParseSPS(payloadDescriptor->width, payloadDescriptor->height);

						break;
					}

					// Aggreation packet.
					// STAP-A.
					case 24:
					{
						size_t offset{ 1 };

						len -= 1;

						// Iterate NAL units.
						while (len >= 3)
						{
							auto naluSize        = Utils::Byte::Get2Bytes(data, offset);
							const uint8_t subnal = *(data + offset + sizeof(naluSize)) & 0x1F;

							if (subnal == 7)
							{
								payloadDescriptor->isKeyFrame = true;

				                // Added by Amir Pauker 02/27/2024 RND-568
				                H264SPSParser spsParser((data + offset + sizeof(naluSize)) + 1, (data + offset + sizeof(naluSize)) + len);
				                spsParser.ParseSPS(payloadDescriptor->width, payloadDescriptor->height);

								break;
							}

							// Check if there is room for the indicated NAL unit size.
							if (len < (naluSize + sizeof(naluSize)))
							{
								break;
							}

							offset += naluSize + sizeof(naluSize);
							len -= naluSize + sizeof(naluSize);
						}

						break;
					}

					// Aggreation packet.
					// FU-A, FU-B.
					case 28:
					case 29:
					{
						const uint8_t subnal   = *(data + 1) & 0x1F;
						const uint8_t startBit = *(data + 1) & 0x80;

						if (subnal == 7 && startBit == 128)
						{
							payloadDescriptor->isKeyFrame = true;

			                // Added by Amir Pauker 02/27/2024 RND-568
			                H264SPSParser spsParser(data + 1, data + len);
			                spsParser.ParseSPS(payloadDescriptor->width, payloadDescriptor->height);
						}

						break;
					}
				}
			}

			return payloadDescriptor.release();
		}

		void H264::ProcessRtpPacket(RTC::RtpPacket* packet)
		{
			MS_TRACE();

			auto* data = packet->GetPayload();
			auto len   = packet->GetPayloadLength();
			RtpPacket::FrameMarking* frameMarking{ nullptr };
			uint8_t frameMarkingLen{ 0 };

			// Read frame-marking.
			packet->ReadFrameMarking(&frameMarking, frameMarkingLen);

			PayloadDescriptor* payloadDescriptor = H264::Parse(data, len, frameMarking, frameMarkingLen);

			if (!payloadDescriptor)
			{
				return;
			}

			auto* payloadDescriptorHandler = new PayloadDescriptorHandler(payloadDescriptor);

			packet->SetPayloadDescriptorHandler(payloadDescriptorHandler);
		}

		/* Instance methods. */

		void H264::PayloadDescriptor::Dump() const
		{
			MS_TRACE();

			MS_DUMP("<PayloadDescriptor>");
			MS_DUMP(
			  "  s:%" PRIu8 "|e:%" PRIu8 "|i:%" PRIu8 "|d:%" PRIu8 "|b:%" PRIu8,
			  this->s,
			  this->e,
			  this->i,
			  this->d,
			  this->b);
			if (this->hasTid)
				MS_DUMP("  tid        : %" PRIu8, this->tid);
			if (this->hasLid)
				MS_DUMP("  lid        : %" PRIu8, this->lid);
			if (this->hasTl0picidx)
				MS_DUMP("  tl0picidx  : %" PRIu8, this->tl0picidx);
			MS_DUMP("  isKeyFrame : %s", this->isKeyFrame ? "true" : "false");
			MS_DUMP("</PayloadDescriptor>");
		}

		H264::PayloadDescriptorHandler::PayloadDescriptorHandler(H264::PayloadDescriptor* payloadDescriptor)
		{
			MS_TRACE();

			this->payloadDescriptor.reset(payloadDescriptor);
		}

		bool H264::PayloadDescriptorHandler::Process(
		  RTC::Codecs::EncodingContext* encodingContext, uint8_t* /*data*/, bool& /*marker*/)
		{
			MS_TRACE();

			auto* context = static_cast<RTC::Codecs::H264::EncodingContext*>(encodingContext);

			MS_ASSERT(context->GetTargetTemporalLayer() >= 0, "target temporal layer cannot be negative");

			// Check if the payload should contain temporal layer info.
			if (context->GetTemporalLayers() > 1 && !this->payloadDescriptor->hasTid)
			{
				MS_DEBUG_DEV("stream is supposed to have  %" PRIu8 " temporal layers but does not have tid field", context->GetTemporalLayers());
			}

			// clang-format off
			if (
				this->payloadDescriptor->hasTid &&
				this->payloadDescriptor->tid > context->GetTargetTemporalLayer()
			)
			// clang-format on
			{
				return false;
			}
			// Upgrade required. Drop current packet if base flag is not set.
			// NOTE: This is possible once this bug in libwebrtc has been fixed:
			//   https://github.com/versatica/mediasoup/issues/306
			//
			// clang-format off
			else if (
				this->payloadDescriptor->hasTid &&
				this->payloadDescriptor->tid > context->GetCurrentTemporalLayer() &&
				!this->payloadDescriptor->b
			)
			// clang-format on
			{
				return false;
			}

			// Update/fix current temporal layer.
			// clang-format off
			if (
				this->payloadDescriptor->hasTid &&
				this->payloadDescriptor->tid > context->GetCurrentTemporalLayer()
			)
			// clang-format on
			{
				context->SetCurrentTemporalLayer(this->payloadDescriptor->tid);
			}
			else if (!this->payloadDescriptor->hasTid)
			{
				context->SetCurrentTemporalLayer(0);
			}

			if (context->GetCurrentTemporalLayer() > context->GetTargetTemporalLayer())
			{
				context->SetCurrentTemporalLayer(context->GetTargetTemporalLayer());
			}

			return true;
		}

		void H264::PayloadDescriptorHandler::Restore(uint8_t* /*data*/)
		{
			MS_TRACE();
		}
	} // namespace Codecs
} // namespace RTC
