import { Logger } from './Logger';
import { EnhancedEventEmitter } from './EnhancedEventEmitter';
import { v4 as uuidv4 } from 'uuid';
import * as ortc from './ortc';
import { Consumer, ConsumerOptions, ConsumerType } from './Consumer';

import { Transport,
	TransportListenIp
} from './Transport';


export interface ShmTransportOptions {
  /**
   * Listening IP address.
   */
  listenIp: TransportListenIp | string;

  shm: any;

  log: any;

  /**
   * Custom application data.
   */
  appData?: Record<string, unknown>;
}

const logger = new Logger('ShmTransport');

export interface ShmTransportStat
{
  // Common to all Transports.
	type: string;
	transportId: string;
	timestamp: number;
	bytesReceived: number;
	recvBitrate: number;
	bytesSent: number;
	sendBitrate: number;
	rtpBytesReceived: number;
	rtpRecvBitrate: number;
	rtpBytesSent: number;
	rtpSendBitrate: number;
	rtxBytesReceived: number;
	rtxRecvBitrate: number;
	rtxBytesSent: number;
	rtxSendBitrate: number;
	probationBytesReceived: number;
	probationRecvBitrate: number;
	probationBytesSent: number;
	probationSendBitrate: number;
	availableOutgoingBitrate?: number;
	availableIncomingBitrate?: number;
  maxIncomingBitrate?: number;
  
  //ShmTrtansport specific
  shm: string;    // shm file name
  writer: number; // writer status: initialized, closed, undefined
}

export class ShmTransport extends Transport
{

	private _shm?: string;

	private _log?: string;

        // Next MID for Consumers. It's converted into string when used.
        #nextMidForConsumers = 0;

	/**
	* @private
	 *
	 */
	constructor(params: any)
	{
		super(params);

		logger.debug('constructor()');

		const { data } = params;

		// ShmTransport data.
		this._shm = data.shm.name;

		this._log = data.shm.log;
	}

	/**
	 * Observer.
	 *
	 * @override
	 * @type {EventEmitter}
	 *
	 * @emits close
	 * @emits {producer: Producer} newproducer
	 * @emits {consumer: Consumer} newconsumer
	 * @emits {producer: DataProducer} newdataproducer
	 * @emits {consumer: DataConsumer} newdataconsumer
	 * @emits {sctpState: String} sctpstatechange
	 */
	/*get observer(): EnhancedEventEmitter
	{
		return this.#observer;
	}*/

	/**
	 * Close the ShmTransport.
	 *
	 * @override
	 */
	close(): void
	{
		if (this.closed)
			return;

			super.close();
	}

	/**
	 * Router was closed.
	 *
	 * @private
	 * @override
	 */
	routerClosed(): void
	{
		if (this.closed)
			return;

		super.routerClosed();
	}

	/**
	 * Get ShmTransport stats.
	 *
	 * @override
	 */
	async getStats(): Promise<ShmTransportStat[]>
	{
		logger.debug('ShmTransport.getStats()');

		return this.channel.request('transport.getStats', this.internal.transportId);
	}

	/**
	 * Provide the ShmTransport remote parameters.
	 *
	 * @param {String} shm- shm name.
	 *
	 * @async
	 * @override
	 */
	async connect(
    {
      shm
    }:
    {
      shm: string
    })
	{
		logger.debug('ShmTransport.connect()');

		const reqData = { shm };

		await this.channel.request('transport.connect', this.internal.transportId, reqData);
	}

	/**
	 * Create a shm Consumer.
	 *
	 * @virtual
	 */
	async consume(
		{
			producerId,
			rtpCapabilities,
			paused = false,
			mid,
			preferredLayers,
			ignoreDtx = false,
			pipe = false,
			appData
		}: ConsumerOptions
	): Promise<Consumer>
	{
		logger.debug('consume()');

		if (!producerId || typeof producerId !== 'string')
			throw new TypeError('missing producerId');
		else if (appData && typeof appData !== 'object')
			throw new TypeError('if given, appData must be an object');
		else if (mid && (typeof mid !== 'string' || mid.length === 0))
			throw new TypeError('if given, mid must be non empty string');

		// This may throw.
		ortc.validateRtpCapabilities(rtpCapabilities!);

		const producer = this.getProducerById(producerId);

		if (!producer)
			throw Error(`Producer with id "${producerId}" not found`);

		// This may throw.
		const rtpParameters = ortc.getConsumerRtpParameters(
			producer.consumableRtpParameters, rtpCapabilities!, pipe);

		// Set MID.
		if (!pipe)
		{
			if (mid)
			{
				rtpParameters.mid = mid;
			}
			else
			{
				rtpParameters.mid = `${this.#nextMidForConsumers++}`;

				// We use up to 8 bytes for MID (string).
				if (this.#nextMidForConsumers === 100000000)
				{
					logger.error(
						`consume() | reaching max MID value "${this.#nextMidForConsumers}"`);

					this.#nextMidForConsumers = 0;
				}
			}
		}

		const consumerId = uuidv4();
		const internal = { ...this.internal, consumerId, producerId };
		const shmData = appData ?
			{
				shm: (appData.shm !== undefined) ? appData.shm : {},
				log: (appData.log !== undefined) ? appData.log : {}
			}
			: {};
		const reqData =
		{
			consumerId,
			producerId,
			kind                   : producer.kind,
			rtpParameters,
			type                   : 'shm',
			shm                    : shmData,
			consumableRtpEncodings : producer.consumableRtpParameters.encodings,
			paused,
			preferredLayers,
			appData,
			ignoreDtx,
		};

		const status =
			await this.channel.request('transport.consume', this.internal.transportId, reqData);

		const data =
		{
			producerId,
			kind : producer.kind,
			rtpParameters,
			type : 'shm' as ConsumerType
		};

		const consumer = new Consumer(
			{
				internal,
				data,
				channel         : this.channel,
				payloadChannel  : this.payloadChannel,
				appData,
				paused          : status.paused,
				producerPaused  : status.producerPaused,
				score           : status.score,
				preferredLayers : status.preferredLayers
			});

		this.consumers.set(consumer.id, consumer);
		consumer.on('@close', () => this.consumers.delete(consumer.id));
		consumer.on('@producerclose', () => this.consumers.delete(consumer.id));

		// Emit observer event.
		this.observer.safeEmit('newconsumer', consumer);

		return consumer;
	}

	/**
	 * Provide the ShmTransport remote parameters.
	 *
	 * @param {Object} meta - metadata string.
	 *
	 * @async
	 */
	async writeStreamMetaData(
		{
      meta
    }:
    {
      meta: string;
    })
	{
		logger.debug('writeStreamMetaData()');

		const reqData = {
			meta,
			shm: this._shm,
			log: this._log
		};

		await this.channel.request('transport.consumeStreamMeta', this.internal.transportId, reqData);
	}

	/**
	 * Does nothing, should not be called like this
	 * 
	 * @private
	 * @override
	 */
	_handleWorkerNotifications(): void
	{
		this.channel.on(this.internal.transportId, async (event, data) => {
			switch (event)
			{
				default:
				{
					logger.error('ignoring unknown event "%s"', event);
				}
			}
		});
	}
}
