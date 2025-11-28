#ifndef F1_25_UDP_H
#define F1_25_UDP_H

#include "PacketSessionHistoryData.h"
#include "PacketMotionData.h"
#include "PacketSessionData.h"
#include "PacketLapData.h"
#include "PacketEventData.h"
#include "PacketParticipantData.h"
#include "PacketCarSetupData.h"
#include "PacketCarTelemetryData.h"
#include "PacketCarStatusData.h"
#include "PacketFinalClassificationData.h"
#include "PacketLobbyInfo.h"
#include "PacketCarDamageData.h"
#include "PacketTyreSetData.h"
#include "PacketMotionEX.h"
#include "PacketTimeTrialData.h"
#include "PacketLapPositions.h" // Se tiver este arquivo

class F1_25_Parser
{
public:
    F1_25_Parser();
    virtual ~F1_25_Parser();
    uint8_t read(void); // <--- IMPORTANTE: uint8_t
    void begin(int port);
    // ... (todos os getters dos pacotes) ...
    PacketMotionData* packetMotionData(void);
    PacketSessionData* packetSessionData(void);
    PacketLapData* packetLapData(void);
    PacketEventData* packetEventData(void);
    PacketParticipantData* packetParticipantData(void);
    PacketCarSetupData* packetCarSetupData(void);
    PacketCarTelemetryData* packetCarTelemetryData(void);
    PacketCarStatusData* packetCarStatusData(void);
    PacketFinalClassificationData* packetFinalClassificationData(void);
    PacketLobbyInfo* packetLobbyData(void);
    PacketCarDamageData* packetCarDamageData(void);
    PacketSessionHistoryData* packetSessionHistoryData(void);
    PacketTyreSetData* packetTyreSetData(void);
    PacketMotionEXData* packetMotionEXData(void);
    PacketTimeTrialData* packetTimeTrialData(void);
    PacketLapPositionsData* packetLapPositionsData(void);

private:
    uint8_t push(char * receiveBuffer); // <--- IMPORTANTE: uint8_t
    // ... (ponteiros dos pacotes) ...
    PacketMotionData* packetMotionData_;
    PacketSessionData* packetSessionData_;
    PacketLapData* packetLapData_;
    PacketEventData* packetEventData_;
    PacketParticipantData* packetParticipantData_;
    PacketCarSetupData* packetCarSetupData_;
    PacketCarTelemetryData* packetCarTelemetryData_;
    PacketCarStatusData* packetCarStatusData_;
    PacketFinalClassificationData* packetFinalClassificationData_;
    PacketLobbyInfo* packetLobbyData_;
    PacketCarDamageData* packetCarDamageData_;
    PacketSessionHistoryData* packetSessionHistoryData_;
    PacketTyreSetData* packetTyreSetData_;
    PacketMotionEXData* packetMotionEXData_;
    PacketTimeTrialData* packetTimeTrialData_;
    PacketLapPositionsData* packetLapPositionsData_;
};

#endif