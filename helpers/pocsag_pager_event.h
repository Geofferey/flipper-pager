#pragma once

typedef enum {
    //PCSGCustomEvent
    PCSGCustomEventStartId = 100,

    PCSGCustomEventSceneSettingLock,
    PCSGCustomEventSceneSettingFrequency,
    PCSGCustomEventSceneSettingCapcodeMin,
    PCSGCustomEventSceneSettingCapcodeMax,
    PCSGCustomEventSceneSettingFrameMin,
    PCSGCustomEventSceneSettingFrameMax,
    PCSGCustomEventSceneSettingRicMin,
    PCSGCustomEventSceneSettingRicMax,
    PCSGCustomEventSceneSettingFreqCorr,
    /** Settings row that opens the HUD options scene. */
    PCSGCustomEventSceneSettingHud,
    PCSGCustomEventSceneCapcodeInputDone,
    PCSGCustomEventTxRicInputDone,
    PCSGCustomEventTxRicInputInvalid,
    PCSGCustomEventTxMessageInputDone,
    PCSGCustomEventTxSend,

    PCSGCustomEventViewReceiverOK,
    PCSGCustomEventViewReceiverFilterAddress,
    PCSGCustomEventViewReceiverConfig,
    /** Right on the live list: what the network has said about itself. */
    PCSGCustomEventViewReceiverNetwork,
    PCSGCustomEventViewReceiverBack,
    PCSGCustomEventViewReceiverOffDisplay,
    PCSGCustomEventViewReceiverUnlock,

    /** Scrolled past the end of the open message: show the next one. */
    PCSGCustomEventViewInfoNextMessage,
    /** Scrolled back past its start: show the previous one, at its end. */
    PCSGCustomEventViewInfoPrevMessage,
    /** Left/right: the next or previous message to the same address. */
    PCSGCustomEventViewInfoNextAddress,
    PCSGCustomEventViewInfoPrevAddress,

    /** Select on the Network screen: swap the clock between NET and RTC. */
    PCSGCustomEventViewNetworkSource,
} PCSGCustomEvent;
