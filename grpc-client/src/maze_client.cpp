/*
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
*/

#include <iostream>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>


#include "maze.grpc.pb.h"
#include <condition_variable>     

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ServerCompletionQueue;
using maze::MazeWalker;
using maze::StepCountReply;
using maze::AdjustStepCountRequest;
using maze::WrongWay;


ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

class MazeClient {
public:
    MazeClient(std::shared_ptr<Channel> channel)
        : stub_(MazeWalker::NewStub(channel)) {}

    enum Direction {
        East, West, North, South
    };

    // Assembles the client's payload, sends it and presents the response back
    // from the server.
    void GoDirection(Direction direction) {
        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        ::google::protobuf::Empty emptyRequest;

        // The actual RPC.
        Status status;
        if (direction == East) 
            status = stub_->GoEast(&context, emptyRequest, &emptyRequest);
        else if (direction == West)
            status = stub_->GoWest(&context, emptyRequest, &emptyRequest);
        else if (direction == North)
            status = stub_->GoNorth(&context, emptyRequest, &emptyRequest);
        else if (direction == South)
            status = stub_->GoSouth(&context, emptyRequest, &emptyRequest);

        if (!status.ok())
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
    }

    int32_t StepCount() {
        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        ::google::protobuf::Empty emptyRequest;

        // Container for the data we expect from the server.
        StepCountReply reply;

        // The actual RPC.
        Status status = stub_->StepCount(&context, emptyRequest, &reply);

        if (!status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return -1;
        }

        return reply.count();
    }

    void AdjustStepCount(int32_t adjustment) {
        ClientContext context;
        AdjustStepCountRequest request;
        request.set_adjustment(adjustment);

        ::google::protobuf::Empty emptyRequest;

        // The actual RPC.
        Status status = stub_->AdjustStepCount(&context, request, &emptyRequest);
        if (!status.ok())
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
    }    

    // Called by reader thread!
    void subscribeForWrongWay() {

        class Reader : public grpc::ClientReadReactor<WrongWay> {
        public:
            Reader(MazeWalker::Stub* stub) {
                ::google::protobuf::Empty emptyRequest;

                stub->async()->Subscribe_WrongWay(&context_, &emptyRequest, this);
                StartRead(&ww_);
                StartCall();
            }
            void OnReadDone(bool ok) override {
                if (ok) {                    
                    const std::string str = ww_.message();
                    std::cout << "WrongWay: " << str << std::endl;
                    std::cout << ">" << std::flush; // Repeat prompt

                    // Read next
                    StartRead(&ww_);
                }
            }
            void OnDone(const Status& s) override {
                std::unique_lock<std::mutex> l(mu_);
                status_ = s;
                done_ = true;
                cv_.notify_one();
            }
            Status Await() {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return done_; });
                return std::move(status_);
            }

            WrongWay ww_;

        private:
            ClientContext context_;
            
            std::mutex mu_;
            std::condition_variable cv_;
            Status status_;
            bool done_ = false;
        };
    
        Reader reader(stub_.get());
        // Block reader thread while the subscribe RPC is active (which it will be until we unsubscribe)
        Status status = reader.Await();
        if (!status.ok()) {            
            std::cout << "Subscribe_WrongWay rpc failed." << std::endl;
        }    
        subscribed = false;
        // Reader thread terminates when we return from here
    }

    // Called by reader thread!
    void subscribeForGoalReached() {

        class Reader : public grpc::ClientReadReactor<StepCountReply> {
        public:
            Reader(MazeWalker::Stub* stub) {
                ::google::protobuf::Empty emptyRequest;

                stub->async()->Subscribe_GoalReached(&context_, &emptyRequest, this);
                StartRead(&scr_);
                StartCall();
            }
            void OnReadDone(bool ok) override {
                if (ok) {
                    const unsigned int stepCount = scr_.count();
                    std::cout << "The goal was reached in " << stepCount << " steps!" << std::endl;
                    std::cout << ">" << std::flush; // Repeat prompt

                    // Read next
                    StartRead(&scr_);
                }
            }
            void OnDone(const Status& s) override {
                std::unique_lock<std::mutex> l(mu_);
                status_ = s;
                done_ = true;
                cv_.notify_one();
            }
            Status Await() {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return done_; });
                return std::move(status_);
            }

            StepCountReply scr_;

        private:
            ClientContext context_;

            std::mutex mu_;
            std::condition_variable cv_;
            Status status_;
            bool done_ = false;
        };

        Reader reader(stub_.get());
        // Block reader thread while the subscribe RPC is active (which it will be until we unsubscribe)
        Status status = reader.Await();
        // We will never get here, since the client never unsubscribes for the GoalReached event
        if (!status.ok()) {
            std::cout << "Subscribe_GoalReached rpc failed." << std::endl;
        }
    }

    void Unsubscribe_WrongWay() {
        ClientContext context;

        ::google::protobuf::Empty emptyRequest;

        Status status = stub_->Unsubscribe_WrongWay(&context, emptyRequest, &emptyRequest);

        if (!status.ok())
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
    }

    std::atomic<bool> subscribed;

private:
    std::unique_ptr<MazeWalker::Stub> stub_;
};

int main(int argc, char** argv) {
    absl::ParseCommandLine(argc, argv);
    // Instantiate the client. It requires a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint specified by
    // the argument "--target=" which is the only expected argument.
    std::string target_str = absl::GetFlag(FLAGS_target);
    // We indicate that the channel isn't authenticated (use of
    // InsecureChannelCredentials()).
    MazeClient mazeWalker(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

    // Launch reader thread to receive notifications when the GoalReached events arrives.
    // Note that we never unsubscribe for this event, so this thread will run until the application terminates
    std::thread subscribeForGoalReached(&MazeClient::subscribeForGoalReached, &mazeWalker);
    subscribeForGoalReached.detach();

    std::cout << "Walk the maze. Commands: " << std::endl;
    std::cout << "east, west, north, south : Take a step in a direction" << std::endl;
    std::cout << "steps : Report number of steps taken" << std::endl;
    std::cout << "adjust : Adjust step count by specified number" << std::endl;
    std::cout << "subscribe : Subscribe to be notified when wrong way taken" << std::endl;
    std::cout << "unsubscribe : Unsubscribe to be notified when wrong way taken" << std::endl;
    std::cout << "exit : Exit" << std::endl;
    std::string command;
    while (true) {
        std::cout << ">" << std::flush;
        std::getline(std::cin, command);
        if (command == "east")
            mazeWalker.GoDirection(MazeClient::East);
        else if (command == "west")
            mazeWalker.GoDirection(MazeClient::West);
        else if (command == "north")
            mazeWalker.GoDirection(MazeClient::North);
        else if (command == "south")
            mazeWalker.GoDirection(MazeClient::South);
        else if (command == "steps") {
            int32_t count = mazeWalker.StepCount();
            std::cout << "Step count: " << count << std::endl;
        }
        else if (command.rfind("adjust", 0) == 0) {
            std::string a = command.substr(command.find(" ") + 1);
            int32_t adjustment = std::stoi(a);
            mazeWalker.AdjustStepCount(adjustment);
        }
        else if (command.rfind("subscribe", 0) == 0) {
            if (mazeWalker.subscribed) {
                std::cout << "You are already subscribed!" << std::endl;
                continue;
            }

            mazeWalker.subscribed = true;

            // Launch reader thread to receive notifications when the WrongWay event arrives
            std::thread subscribeForWrongWayThread(&MazeClient::subscribeForWrongWay, &mazeWalker);
            subscribeForWrongWayThread.detach();
        }
        else if (command.rfind("unsubscribe", 0) == 0) {
            if (!mazeWalker.subscribed) {
                std::cout << "You are already unsubscribed!" << std::endl;
                continue;
            }

            mazeWalker.Unsubscribe_WrongWay();
        }
        else if (command == "exit")
            return 0;
        else {
            std::cout << "Unknown command '" << command << "'" << std::endl;
        }
    }
}