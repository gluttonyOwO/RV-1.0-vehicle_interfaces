#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#include "rclcpp/rclcpp.hpp"
#include "vehicle_interfaces/msg/header.hpp"
#include "vehicle_interfaces/srv/time_sync.hpp"

#ifndef ROS_DISTRO// 0: eloquent, 1: foxy, 2: humble
#define ROS_DISTRO 1
#endif

using namespace std::chrono_literals;

class Timer
{
private:
    std::chrono::high_resolution_clock::duration interval_;
    std::chrono::high_resolution_clock::time_point st_;

    std::atomic<bool> activateF_;
    std::atomic<bool> exitF_;
    std::atomic<bool> funcCallableF_;
    std::function<void()> func_;

    std::thread timerTH_;
    std::thread callbackTH_;

private:
	template <typename T>
	void _safeSave(T* ptr, const T value, std::mutex& lock)
	{
		std::lock_guard<std::mutex> _lock(lock);
		*ptr = value;
	}

	template <typename T>
	T _safeCall(const T* ptr, std::mutex& lock)
	{
		std::lock_guard<std::mutex> _lock(lock);
		return *ptr;
	}

    void _timer()// Deprecated
    {
        while (!this->exitF_)
        {
            try
            {
                auto st = std::chrono::high_resolution_clock::now();
                while (!this->exitF_ && this->activateF_ && (std::chrono::high_resolution_clock::now() - st < this->interval_))
                    std::this_thread::yield();
                if (!this->exitF_ && this->activateF_ && this->funcCallableF_)
                {
                    if (this->callbackTH_.joinable())
                        this->callbackTH_.join();
                    this->callbackTH_ = std::thread(&Timer::_tick, this);
                }
                std::this_thread::yield();
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        if (this->callbackTH_.joinable())
            this->callbackTH_.join();
    }

    void _timer_fixedRate()
    {
        while (!this->exitF_)
        {
            try
            {
                if (!this->exitF_ && this->activateF_)
                {
                    auto tickTimePoint = this->st_ + this->interval_;
                    this->st_ = tickTimePoint;

                    while (!this->exitF_ && this->activateF_ && (std::chrono::high_resolution_clock::now() < tickTimePoint))
                        std::this_thread::yield();
                    if (!this->exitF_ && this->activateF_ && this->funcCallableF_)
                    {
                        if (this->callbackTH_.joinable())
                            this->callbackTH_.join();
                        this->callbackTH_ = std::thread(&Timer::_tick, this);
                    }
                }
                std::this_thread::yield();
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        if (this->callbackTH_.joinable())
            this->callbackTH_.join();
    }

    void _tick()
    {
        this->funcCallableF_ = false;
        this->func_();
        this->funcCallableF_ = true;
    }

public:
    Timer(double interval_ms, const std::function<void()>& callback) : activateF_(false), exitF_(false), funcCallableF_(true)
    {
        this->interval_ = std::chrono::high_resolution_clock::duration(std::chrono::nanoseconds(static_cast<uint64_t>(interval_ms * 1000000)));
        this->func_ = callback;
        this->st_ = std::chrono::high_resolution_clock::now();
        this->timerTH_ = std::thread(&Timer::_timer_fixedRate, this);
    }

    ~Timer()
    {
        this->destroy();
    }

    void start()
    {
        this->st_ = std::chrono::high_resolution_clock::now();
        this->activateF_ = true;
    }

    void stop() { this->activateF_ = false; }

    void setInterval(double interval_ms)
    {
        bool preState = this->activateF_;
        this->stop();
        this->interval_ = std::chrono::high_resolution_clock::duration(std::chrono::nanoseconds(static_cast<uint64_t>(interval_ms * 1000000)));
        if (preState)
            this->start();
    }

    void destroy()
    {
        this->activateF_ = false;
        this->exitF_ = true;
        if (this->timerTH_.joinable())
            this->timerTH_.join();
    }
};

class TimeSyncNode : virtual public rclcpp::Node
{


private:
    rclcpp::Time initTime_;
    rclcpp::Time refTime_;
    rclcpp::Duration* correctDuration_;
    uint8_t timeStampType_;
    
    std::shared_ptr<rclcpp::Node> clientNode_;
    rclcpp::Client<vehicle_interfaces::srv::TimeSync>::SharedPtr client_;

    Timer* timeSyncTimer_;
    std::chrono::duration<double, std::milli> timeSyncTimerInterval_;
    std::chrono::duration<double, std::nano> timeSyncAccuracy_;

    std::atomic<bool> isSyncF_;
    std::mutex correctDurationLock_;

private:
	template <typename T>
	void _safeSave(T* ptr, const T value, std::mutex& lock)
	{
		std::lock_guard<std::mutex> _lock(lock);
		*ptr = value;
	}

	template <typename T>
	T _safeCall(const T* ptr, std::mutex& lock)
	{
		std::lock_guard<std::mutex> _lock(lock);
		return *ptr;
	}

    void connToService()
    {
        while (!this->client_->wait_for_service(1s))
        {
            if (!rclcpp::ok())
            {
                RCLCPP_ERROR(this->clientNode_->get_logger(), "Interrupted while waiting for the service. Exiting.");
                return;
            }
            RCLCPP_INFO(this->clientNode_->get_logger(), "service not available, waiting again...");
        }
    }

    void timeSyncTimer_callback_()
    {
        std::cout << "timeSyncTimer_callback_" << std::endl;
        auto st = std::chrono::high_resolution_clock::now();
        try
        {
            while (!this->syncTime() && (std::chrono::high_resolution_clock::now() - st < this->timeSyncTimerInterval_))
                std::this_thread::sleep_for(500ms);
            if (!this->isSyncF_)
                printf("[TimeSyncNode::timeSyncTimer_callback_] Time sync failed.\n");
            else
                printf("[TimeSyncNode::timeSyncTimer_callback_] Time synced.\n");
            printf("[TimeSyncNode::timeSyncTimer_callback_] Correct duration: %f us\n", this->_safeCall(this->correctDuration_, this->correctDurationLock_).nanoseconds() / 1000.0);
        }
        catch (const std::exception& e)
        {
            std::cerr << "[TimeSyncNode::timeSyncTimer_callback_] Unexpected Error" << e.what() << std::endl;
        }
    }

public:
    TimeSyncNode(const std::string& nodeName, 
                    const std::string& timeServiceName, 
                    double syncInterval_ms, 
                    double syncAccuracy_ms) : rclcpp::Node(nodeName)
    {
        this->clientNode_ = rclcpp::Node::make_shared(nodeName + "_timesync_client");
        this->client_ = this->clientNode_->create_client<vehicle_interfaces::srv::TimeSync>(timeServiceName);
        this->isSyncF_ = false;
        this->initTime_ = rclcpp::Time();
        this->refTime_ = rclcpp::Time();
        this->correctDuration_ = new rclcpp::Duration(0, 0);
        this->timeStampType_ = vehicle_interfaces::msg::Header::STAMPTYPE_NO_SYNC;
        this->timeSyncAccuracy_ = std::chrono::duration<double, std::nano>(syncAccuracy_ms * 1000000.0);
        
        printf("[TimeSyncNode] Sync time from %s...\n", timeServiceName.c_str());
        this->connToService();
        std::this_thread::sleep_for(200ms);
        try
        {
            while (!this->syncTime())
                std::this_thread::sleep_for(500ms);
        }
        catch (const std::exception& e)
        {
            std::cerr << "[TimeSyncNode] Unexpected Error" << e.what() << std::endl;
        }
        printf("[TimeSyncNode] Time synced: %d\n", this->isSyncF_.load());

        if (syncInterval_ms > 0)
        {
            this->timeSyncTimerInterval_ = std::chrono::duration<double, std::milli>(syncInterval_ms);
            this->timeSyncTimer_ = new Timer(syncInterval_ms, std::bind(&TimeSyncNode::timeSyncTimer_callback_, this));
            this->timeSyncTimer_->start();
        }
    }

    bool syncTime()
    {
        try
        {
            this->isSyncF_ = false;
            auto request = std::make_shared<vehicle_interfaces::srv::TimeSync::Request>();
            request->request_code = this->timeStampType_;
            request->request_time = this->get_clock()->now();
            auto result = this->client_->async_send_request(request);
            // Wait for the result.

#if ROS_DISTRO == 0
            if (rclcpp::spin_until_future_complete(this->clientNode_, result, 10ms) == rclcpp::executor::FutureReturnCode::SUCCESS)
#else
            if (rclcpp::spin_until_future_complete(this->clientNode_, result, 10ms) == rclcpp::FutureReturnCode::SUCCESS)
#endif
            {
                rclcpp::Time nowTime = this->get_clock()->now();
                auto response = result.get();
                this->initTime_ = response->request_time;
                if ((nowTime - this->initTime_).nanoseconds() >  this->timeSyncAccuracy_.count())// If travel time > accuracy, re-sync
                    return false;
                
                this->refTime_ = (rclcpp::Time)response->response_time - (nowTime - this->initTime_) * 0.5;
                this->timeStampType_ = response->response_code;
                if (this->timeStampType_ == vehicle_interfaces::msg::Header::STAMPTYPE_NO_SYNC)
                    throw vehicle_interfaces::msg::Header::STAMPTYPE_NO_SYNC;
                
                this->_safeSave(this->correctDuration_, this->refTime_ - this->initTime_, this->correctDurationLock_);
                
                RCLCPP_INFO(this->clientNode_->get_logger(), "Response: %d", this->timeStampType_);
                RCLCPP_INFO(this->clientNode_->get_logger(), "Local time: %f s", this->initTime_.seconds());
                RCLCPP_INFO(this->clientNode_->get_logger(), "Reference time: %f s", this->refTime_.seconds());
                RCLCPP_INFO(this->clientNode_->get_logger(), "Transport time: %f ms", (nowTime - this->initTime_).nanoseconds() / 1000000.0);
                RCLCPP_INFO(this->clientNode_->get_logger(), "Correct duration: %f us",this->correctDuration_->nanoseconds() / 1000.0);
                this->isSyncF_ = true;
                return true;
            }
            else
            {
                RCLCPP_ERROR(this->clientNode_->get_logger(), "Failed to call service");
                return false;
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << "[TimeSyncNode::syncTime] Unexpected Error" << e.what() << std::endl;
            this->isSyncF_ = false;
            throw e;
        }
    }

    rclcpp::Time getTimestamp()
    {
        return this->get_clock()->now() + this->_safeCall(this->correctDuration_, this->correctDurationLock_);
    }
};

class TimeSyncServer : public rclcpp::Node
{
private:
    rclcpp::Service<vehicle_interfaces::srv::TimeSync>::SharedPtr server_;
    bool isUTCSync;

private:
    void _serviceCallback(const std::shared_ptr<vehicle_interfaces::srv::TimeSync::Request> request, 
                            std::shared_ptr<vehicle_interfaces::srv::TimeSync::Response> response)
    {
        RCLCPP_INFO(this->get_logger(), "request: %d", request->request_code);
        response->request_time = request->request_time;
        response->response_time = this->get_clock()->now();
        //printf("req time: %f\n", response->request_time.stamp.sec + response->request_time.stamp.nanosec / 1000000000.0);
        //printf("res time: %f\n", response->response_time.stamp.sec + response->response_time.stamp.nanosec / 1000000000.0);
        
        if (!this->isUTCSync)
            response->response_code = vehicle_interfaces::msg::Header::STAMPTYPE_NONE_UTC_SYNC;
        else
            response->response_code = vehicle_interfaces::msg::Header::STAMPTYPE_UTC_SYNC;
        
    }

public:
    TimeSyncServer(std::string nodeName, std::string timeServiceName) : rclcpp::Node(nodeName)
    {
        this->server_ = this->create_service<vehicle_interfaces::srv::TimeSync>(timeServiceName, 
            std::bind(&TimeSyncServer::_serviceCallback, this, std::placeholders::_1, std::placeholders::_2));
        this->isUTCSync = false;
        RCLCPP_INFO(this->get_logger(), "[TimeSyncServer] Constructed");
    }
};