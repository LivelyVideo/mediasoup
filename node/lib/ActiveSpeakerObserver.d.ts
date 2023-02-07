import { EnhancedEventEmitter } from './EnhancedEventEmitter';
import { RtpObserver, RtpObserverEvents, RtpObserverObserverEvents, RtpObserverConstructorOptions } from './RtpObserver';
import { Producer } from './Producer';
export type ActiveSpeakerObserverOptions = {
    interval?: number;
    /**
     * Custom application data.
     */
    appData?: Record<string, unknown>;
};
export type ActiveSpeakerObserverDominantSpeaker = {
    /**
     * The audio Producer instance.
     */
    producer: Producer;
};
export type ActiveSpeakerObserverEvents = RtpObserverEvents & {
    dominantspeaker: [ActiveSpeakerObserverDominantSpeaker];
};
export type ActiveSpeakerObserverObserverEvents = RtpObserverObserverEvents & {
    dominantspeaker: [ActiveSpeakerObserverDominantSpeaker];
};
type RtpObserverObserverConstructorOptions = RtpObserverConstructorOptions;
export declare class ActiveSpeakerObserver extends RtpObserver<ActiveSpeakerObserverEvents> {
    /**
     * @private
     */
    constructor(options: RtpObserverObserverConstructorOptions);
    /**
     * Observer.
     */
    get observer(): EnhancedEventEmitter<ActiveSpeakerObserverObserverEvents>;
    private handleWorkerNotifications;
}
export {};
//# sourceMappingURL=ActiveSpeakerObserver.d.ts.map