#ifndef SDLP_TELECOMMAND
#define SDLP_TELECOMMAND

#include "../../lib/basicdty.h"
#include "tf.h"

class Telecommand {
private: 
public: 
// ---------------------- MAPP Service ----------------------
    /* MAPP.request. Parameters: 
     * Packet
     * GVCID
     * MAP ID
     * Packet Version Number
     * SDU ID
     * Service Type: 1 octet. 
    */
    void request(Telecommand_TF packet, U32 GVCID, U16 MAP_ID, U8 PVN, U8 SDU_ID, U8 Service_Type) {
    }
    /* MAPP_Notify.indication. Parameters: 
     * GVCID
     * MAP ID
     * Packet Version Number
     * SDU ID
     * Service Type: 1 octet. 
     * Notification Type: 1 octet. 
    */
    void indication(U32 GVCID, U16 MAP_ID, U8 PVN, U8 SDU_ID, U8 Service_Type, U8 Notification_Type) {
    }

// ---------------------- VCA Service ----------------------

}; 

#endif
