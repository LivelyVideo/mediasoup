import EnhancedEventEmitter from './EnhancedEventEmitter';
import Transport, { TransportListenIp } from './Transport';
export interface ShmTransportOptions {
    /**
     * Listening IP address.
     */
    listenIp: TransportListenIp | string;
    shmName: string;
    logName: string;
    logLevel: number;
    /**
     * Custom application data.
     */
    appData?: any;
}
export interface ShmTransportStat {
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
    shm: string;
    writer: number;
}
export default class ShmTransport extends Transport {
    /**
     * @private
     *
     * @emits {sctpState: String} sctpstatechange
     */
    constructor(params: any);
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
    get observer(): EnhancedEventEmitter;
    /**
     * Close the ShmTransport.
     *
     * @override
     */
    close(): void;
    /**
     * Router was closed.
     *
     * @private
     * @override
     */
    routerClosed(): void;
    /**
     * Get PipeTransport stats.
     *
     * @override
     */
    getStats(): Promise<ShmTransportStat[]>;
    /**
     * Provide the ShmTransport remote parameters.
     *
     * @param {String} shm- shm name.
     *
     * @async
     * @override
     */
    connect({ shm }: {
        shm: string;
    }): Promise<void>;
    /**
     * @private
     * @override
     */
    _handleWorkerNotifications(): Promise<void>;
}
//# sourceMappingURL=ShmTransport.d.ts.map