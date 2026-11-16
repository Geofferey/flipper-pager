// The two apps built from this tree carry only their own scenes. Everything
// generated from this list - the handler arrays and the scene id enum - follows
// automatically, so a scene left out of a build costs it nothing.
#ifdef PCSG_TX_APP
ADD_SCENE(pocsag_pager, tx_config, TxConfig)
ADD_SCENE(pocsag_pager, tx_params, TxParams)
ADD_SCENE(pocsag_pager, tx_ric_input, TxRicInput)
ADD_SCENE(pocsag_pager, tx_message_input, TxMessageInput)
ADD_SCENE(pocsag_pager, tx_confirm, TxConfirm)
// Typed frequencies and the correction step share this with receiver config.
ADD_SCENE(pocsag_pager, capcode_input, CapcodeInput)
#else
ADD_SCENE(pocsag_pager, start, Start)
ADD_SCENE(pocsag_pager, about, About)
ADD_SCENE(pocsag_pager, receiver, Receiver)
ADD_SCENE(pocsag_pager, receiver_config, ReceiverConfig)
ADD_SCENE(pocsag_pager, receiver_info, ReceiverInfo)
ADD_SCENE(pocsag_pager, network_info, NetworkInfo)
ADD_SCENE(pocsag_pager, hud_config, HudConfig)
ADD_SCENE(pocsag_pager, capcode_input, CapcodeInput)
ADD_SCENE(pocsag_pager, review, Review)
ADD_SCENE(pocsag_pager, review_info, ReviewInfo)
#endif
