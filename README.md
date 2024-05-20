# gRPC Sample
[gRPC](https://grpc.io/) is a Remote Procedure Call (RPC) framework for building systems of distributed applications in heterogeneous environments. This sample shows how you can use it in applications created with [DevOps Model RealTime](https://www.hcl-software.com/devops-model-realtime).

This sample contains a [gRPC server](/grpc-server) (implemented with Model RealTime) and a [gRPC client](/grpc-client) (implemented as a C++ command-line application).

## Preparations
1. Download, build and install gRPC

Follow the steps in the [gRPC QuickStart guide](https://grpc.io/docs/languages/cpp/quickstart/) to install required build tools, clone the [gRPC GitHub repo](https://github.com/grpc/grpc), build gRPC from its source code and install the gRPC tools and libraries. Windows users can find more detailed instructions [here](https://github.com/grpc/grpc/blob/v1.61.0/BUILDING.md).

**NOTE 1:** The sample is by default configured to be built with Visual Studio 17 (2022) using cmake for Win64 Debug. If you want to use a different compiler, target platform or build configuration you must adjust arguments to cmake when building gRPC. You then also must update the TC in the server project and use an appropriate cmake generator for building the client accordingly. The further steps assume you have the correct build tools in the path (for Visual Studio this is best accomplished by using a Visual Studio command prompt).

**NOTE 2:** If you go with the default build configuration you should also build the TargetRTS with the same settings. Follow [these steps](https://model-realtime.hcldoc.com/help/topic/com.ibm.xtools.rsarte.webdoc/Articles/Running%20and%20debugging/Debugging%20the%20RT%20services%20library.html?cp=23_2_13_1). In addition, set the `/MDd` flag in `LIBSETCCFLAGS` when you edit `libset.mk` before building it.
 
2. Add the location where you installed the gRPC tools to your PATH environment variable

## Build the client
3. You need to build the client before the server since it contains the [.proto file](/grpc-client/proto/maze.proto) that describes the RPCs implemented by the server. This file is used both by the client and the server.

```
..grpc-client> mkdir build
..grpc-client> cd build
Windows:
..grpc-client/build> cmake .. -G "Visual Studio 17 2022"
..grpc-client/build> cmake --build . --config Debug

Linux:
cmake .. -DCMAKE_INSTALL_PREFIX=GRPC Install location
cmake --build . --config Debug

```

This will generate a Visual Studio solution file (`MazeWalker.sln`) which you can use for debugging the client application.

## Build the server
4. Open the [gRPC server](/grpc-server) project in Model RealTime.
5. Open the TC file [`server.tcjs`](/grpc-server/server.tcjs) and edit the variables in the beginning of the file that specify the location where you placed the gRPC source code (`grpcSourceLocation`) and where you installed the gRPC tools and libraries (`grpcInstallLocation`).
6. Also edit the `targetServicesLibrary` property to specify the path to the TargetRTS to use. If you want to use the default TargetRTS that comes with Model RealTime, you can delete this property, but remember that with that version you cannot debug. Other properties that have to be modified depending on if you want to debug or not (and which compiler that is used) are `compileArguments` and `linkArguments`. By default they are set so you can debug with Visual Studio.
7. Build the TC.

## Run the sample
The sample application is about walking a maze represented with a state machine in the `Maze` capsule. From a state in the maze you can either go east, west, north or south. Valid paths are represented by transitions. If you find your way out of the maze (not very hard) you reach the goal.

![](images/maze.png)

The capsule has a `stepCount` variable which keeps track of the number of steps taken in the maze.

Launch the server executable that was generated when building the TC. If you launch with the Model Debugger you can visually inspect how the active state changes in the maze, and how the `stepCount` variable gets updated, as a result of client requests.

Finally launch the client executable `maze_client.exe` that was built previously. It has a simple command prompt where you can send commands to the server for walking the maze:

* **east, west, north, south** Take a step in the direction (if possible)
* **steps** Report the number of steps taken so far
* **adjust <arg>** Adjust the step counter with the specified argument (an integer)
* **subscribe** Subscribe to get notified about an attempt to take the wrong way in the maze
* **unsubscribe** Unsubscribe from the notifications about taking the wrong way in the maze
* **exit** Terminate the client

**NOTE:** The client and server communicate on port 50051 on localhost (i.e. you must run them on the same machine). You can change the port and hostname by means of the `--target` command-line argument for the client. However, the same is currently hardcoded in the server (set the default value of `GRPC_Server::port`).

For example, to connect to the server running on a different machine and on a different port:
```
maze_client.exe --target 172.27.223.1:50052
```

## How the sample works
Refer to the [.proto file](/grpc-client/proto/maze.proto) and the picture below to understand how the sample works:

![](images/server_client.png)

### Asynchronous request without data
The "west", "east", "north" and "south" commands will cause an event to be asynchronously sent. They don't have a data parameter.

```protobuf
// Send an event without data
rpc GoWest (google.protobuf.Empty) returns (google.protobuf.Empty) {}
rpc GoEast (google.protobuf.Empty) returns (google.protobuf.Empty) {}
rpc GoNorth (google.protobuf.Empty) returns (google.protobuf.Empty) {}
rpc GoSouth (google.protobuf.Empty) returns (google.protobuf.Empty) {}
```

When the client sends these requests they will be intercepted by the server's thread (`ServerJob`) which will transfer them to the `gRPC_Server` capsule through its `external` port. Each request is represented by a subclass of the `CallData` class which implements a simple state machine describing the life cycle of an RPC request. The `gRPC_Server` capsule, which is run by the server's main thread, will then perform the sending of the corresponding event from the `Commands` protocol (`west()`, `east()`, `north()` and `south` respectively). This happens in  `CallData_Go::completeRequest()`. Note that since these events are asynchronously sent, the client can immediately proceed its execution and doesn't need to wait for a reply.

### Asynchronous request with data
The "adjust" command takes an integer as argument and the corresponding RPC therefore has an input message for transfering that data.

```protobuf
// Send an event with data
rpc AdjustStepCount (AdjustStepCountRequest) returns (google.protobuf.Empty) {}

// Adjustment of the step counter
message AdjustStepCountRequest {
  int32 adjustment = 1;
}
```

This request is handled by the server in exactly the same way as described above. The only difference is that the data that is passed is used as data for the parameter of the `adjustStepCount()` event. This happens in  `CallData_AdjustStepCount::completeRequest()`. 

### Synchronous request with reply data
The "steps" command causes the client to make a synchronous request to the server for getting the current step count. 

```protobuf
// Invoke an event with reply data
rpc StepCount (google.protobuf.Empty) returns (StepCountReply) {}

// Number of steps taken
message StepCountReply {
  int32 count = 1;
}
```

The server handles this request in the same way as the asynchronous requests, but the client will wait until the server finishes the request and provides the reply data (which is a simple integer). This happens in  `CallData_StepCount::completeRequest()`. 

### Subscribing for outgoing events
The client can subscribe to get notified by the server when an outgoing event is received by the `gRPC_Server` capsule on its `commands` port. 

```protobuf
// Subscribe for outgoing event with data
rpc Subscribe_WrongWay (google.protobuf.Empty) returns (stream WrongWay) {}
rpc Subscribe_GoalReached (google.protobuf.Empty) returns (stream StepCountReply) {} // Messages can be "reused"

  // Outgoing event 
message WrongWay {
  string message = 1;
}
  ```

  Subscription requests are managed by the server in a slightly different way than the requests covered so far. A subclass of `SubscribeData` is used for representing a subscription request. When it reaches the `gRPC_Server` capsule it's not immediately completed like other requests. Instead it is inserted into a map with an id string as key. The sample uses the name of the event ("GoalReached", "WrongWay") as the id string, but any id that uniquely identifies the event that is subscribed for can be used.

  Internal transitions in the `WaitForRequest` state of the `MyGRPC_Server` capsule get triggered when outgoing events arrive. They call `GRPC_Server::getSubscription()` for checking if there currently is an active subscription for the received event. If so, `SubscribeData::notifySubscriber()` is called on the subscription request object. This is where the server notifies the client about the received outgoing event by writing a reply for the subscription request.

  Note that the client needs to launch a separate thread which can wait for notifications from active subscriptions, without blocking the main thread. In the sample, the client uses one such thread for each outgoing event it subscribes to (at most two), but other alternatives, such as using a single thread for managing notifications from all active subscriptions, would also be possible.

### Unsubscribing for outgoing events
The server may offer a way for the client to unsubscribe from getting notified for outgoing events. In the sample, the server offers this for the `wrongWay()` event.

```protobuf
// Unsubscribe for outgoing event
rpc Unsubscribe_WrongWay (google.protobuf.Empty) returns (google.protobuf.Empty) {}
```

This is a regular request which is handled exactly like other non-subscription requests. From `CallData_Unsubscribe_WrongWay::completeRequest()` the subscription request is removed from the `gRPC_Server` capsule's map of active subscriptions by a call to `GRPC_Server::unsubscribe()`. Then the subscription request is finished, see `SubscribeData_WrongWay_unsubscribe()`.

In the sample client, the finishing of a subscription request will terminate the thread that was launched for managing that subscription while it was active.