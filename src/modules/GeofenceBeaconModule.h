#pragma once

/*
 * GeofenceBeaconModule
 *
 * OSThread + SinglePortModule that, while a TX gate is active in
 * Router::send, is the only originator allowed to transmit. A canned
 * TEXT_MESSAGE_APP packet is sent only when the geofence and/or time
 * window triggers are satisfied.
 *
 * Pattern follows DetectionSensorModule (SinglePortModule + OSThread)
 * and RangeTestModule (periodic send via allocDataPacket +
 * service->sendToMesh).
 *
 * UNTESTED. Do not treat this as a compile-verified drop-in. Re-check
 * includes, PortNum, and GPS accessors against your firmware checkout.
 */

#include "GeofenceBeaconConfig.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

class GeofenceBeaconModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    GeofenceBeaconModule();

  protected:
    virtual int32_t runOnce() override;

    // This module only originates beacons; it does not handle inbound text.
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool firstTime = true;
    bool insideFence = false;
    uint32_t lastBeaconMs = 0;

    bool hasUsablePosition(int32_t &lat_i, int32_t &lon_i) const;
    bool hasUsableTime(uint32_t &epochSecs) const;
    bool inTimeWindow(uint32_t epochSecs) const;
    float distanceMeters(int32_t lat_i, int32_t lon_i) const;
    void updateHysteresis(float distMeters);
    bool triggersSatisfied(bool geoActive, bool timeActive) const;
    bool intervalElapsed() const;
    void sendCannedMessage();
};

extern GeofenceBeaconModule *geofenceBeaconModule;
