#ifndef MS_RTC_CODECS_H264_HPP
#define MS_RTC_CODECS_H264_HPP

#include "common.hpp"
#include "RTC/Codecs/PayloadDescriptorHandler.hpp"
#include "RTC/RtpPacket.hpp"

namespace RTC
{
	namespace Codecs
	{
	    /**********************************************************************
	     *                Added by Amir Pauker 02/27/2024 RND-568
	     *********************************************************************/

	class H264SPSParser {
	private:
	    const uint8_t     *pos;
	    const uint8_t     *last;
	    uint64_t          offs;
	    uint64_t          err;
	public:
	    H264SPSParser(const uint8_t *pos, const uint8_t *last)
	    {
	        this->pos = pos;
	        this->last = last;
	        offs = err = 0;
	    }
	    uint64_t Read(uint64_t n);
	    uint64_t ReadGolomb();
	    void ParseSPS(uint16_t &widthOut, uint16_t &heightOut);
	};

        /**********************************************************************
         *********************************************************************/

		class H264
		{
		public:
			struct PayloadDescriptor : public RTC::Codecs::PayloadDescriptor
			{
				/* Pure virtual methods inherited from RTC::Codecs::PayloadDescriptor. */
				~PayloadDescriptor() = default;

				void Dump() const override;

				// Fields in frame-marking extension.
				uint8_t s : 1;          // Start of Frame.
				uint8_t e : 1;          // End of Frame.
				uint8_t i : 1;          // Independent Frame.
				uint8_t d : 1;          // Discardable Frame.
				uint8_t b : 1;          // Base Layer Sync.
				uint8_t tid{ 0 };       // Temporal layer id.
				uint8_t lid{ 0 };       // Spatial layer id.
				uint8_t tl0picidx{ 0 }; // TL0PICIDX
				// Parsed values.
				bool hasLid{ false };
				bool hasTid{ false };
				bool hasTl0picidx{ false };
				bool isKeyFrame{ false };

				// Added by Amir Pauker 02/27/2024 RND-568
				uint16_t width {0};
				uint16_t height {0};
			};

		public:
			static H264::PayloadDescriptor* Parse(
			  const uint8_t* data,
			  size_t len,
			  RTC::RtpPacket::FrameMarking* frameMarking = nullptr,
			  uint8_t frameMarkingLen                    = 0);
			static void ProcessRtpPacket(RTC::RtpPacket* packet);

		public:
			class EncodingContext : public RTC::Codecs::EncodingContext
			{
			public:
				explicit EncodingContext(RTC::Codecs::EncodingContext::Params& params)
				  : RTC::Codecs::EncodingContext(params)
				{
				}
				~EncodingContext() = default;

				/* Pure virtual methods inherited from RTC::Codecs::EncodingContext. */
			public:
				void SyncRequired() override
				{
				}
			};

		public:
			class PayloadDescriptorHandler : public RTC::Codecs::PayloadDescriptorHandler
			{
			public:
				explicit PayloadDescriptorHandler(PayloadDescriptor* payloadDescriptor);
				~PayloadDescriptorHandler() = default;

			public:
				void Dump() const override
				{
					this->payloadDescriptor->Dump();
				}
				bool Process(RTC::Codecs::EncodingContext* encodingContext, uint8_t* data, bool& marker) override;
				void Restore(uint8_t* data) override;
				uint8_t GetSpatialLayer() const override
				{
					return 0u;
				}
				uint8_t GetTemporalLayer() const override
				{
					return this->payloadDescriptor->tid;
				}
				bool IsKeyFrame() const override
				{
					return this->payloadDescriptor->isKeyFrame;
				}

				// Added by Amir Pauker 02/27/2024 RND-568
				uint16_t GetWidth() const override
				{
				    return this->payloadDescriptor->width;
				}
                uint16_t GetHeight() const override
                {
                    return this->payloadDescriptor->height;
                }

			private:
				std::unique_ptr<PayloadDescriptor> payloadDescriptor;
			};
		};
	} // namespace Codecs
} // namespace RTC

#endif
