
typedef enum ActionTypes
    { SvcParentResources, MsgAttention, MsgRelay, MsgSwiPwr, MsgAskTemper, MsgAskRHumid } ActionType;
typedef enum ResourceTypes
    { Free, AirConditioner, Heater, DeHumidifier, AirExchangerIn, AirExchangerOut, Humidifier, InternalFan } ResourceType;
typedef enum OutputTypes
    { Relay1, Relay2, SwiPwr1, SwiPwr2  } OutputType;

ActionType actionType;      // These guys have file scope. They are prepared by indexNextResource() et al,
ResourceType resourceType;  // then used by messageXX()
OutputType outputType;