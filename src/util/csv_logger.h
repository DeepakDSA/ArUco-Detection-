#pragma once

#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <filesystem>
#include <cstdlib>
#include <chrono>
#include <sstream>

#include "../processing/motion_types.h"

// Asynchronous CSV logger that writes one line per processed frame.
// Uses a background thread to minimize impact on FPS.
class CsvLogger {
public:
	static CsvLogger& instance() {
		static CsvLogger inst;
		return inst;
	}

	// Initialize output directory and CSV file.
	void init(const std::string& out_dir = defaultOutDir()) {
		std::lock_guard<std::mutex> lk(init_m_);
		if (initialized_) return;
		out_dir_ = out_dir;
		std::filesystem::create_directories(out_dir_);
		csv_path_ = out_dir_ + "/metrics.csv";
		// create file with header
		bool need_header = !std::filesystem::exists(csv_path_);
		ofs_.open(csv_path_, std::ios::out | std::ios::app);
		if (!ofs_.is_open()) {
			// cannot write; leave initialized false
			return;
		}
		if (need_header) {
			ofs_ << "ts_us,tracking,marker_id,bbox_x,bbox_y,bbox_w,bbox_h";
			for (int i=0;i<4;i++) {
				ofs_ << ",q" << i << "_valid,q" << i << "_cx,q" << i << "_cy,q" << i << "_vx,q" << i << "_vy,q" << i << "_ax,q" << i << "_ay";
			}
			ofs_ << "\n";
		}
		running_ = true;
		worker_ = std::thread([this]{ this->run(); });
		initialized_ = true;
	}

	// Log one line. Thread-safe, non-blocking (bounded queue).
	void log(uint64_t ts_us, const TrackerState& st) {
		if (!initialized_) init();
		if (!running_) return;
		std::string line = build_line(ts_us, st);
		{
			std::unique_lock<std::mutex> lk(m_);
			if (queue_.size() >= max_queue_) {
				// drop oldest to keep up
				queue_.pop();
			}
			queue_.push(std::move(line));
		}
		cv_.notify_one();
	}

	void shutdown() {
		running_ = false;
		cv_.notify_all();
		if (worker_.joinable()) worker_.join();
		if (ofs_.is_open()) ofs_.close();
	}

	const std::string& outDir() const { return out_dir_; }
	const std::string& csvPath() const { return csv_path_; }

private:
	CsvLogger() = default;
	~CsvLogger() { shutdown(); }

	static std::string defaultOutDir() {
		const char* env = std::getenv("ARUCO_OUT_DIR");
		if (env && *env) return std::string(env);
		return std::string("/data/yash_project/frames");
	}

	std::string build_line(uint64_t ts_us, const TrackerState& st) {
		std::ostringstream os;
		os.setf(std::ios::fixed); os.precision(3);
		os << ts_us << "," << (st.tracking ? 1 : 0) << "," << st.marker_id
		   << "," << st.marker_bbox.x << "," << st.marker_bbox.y << "," << st.marker_bbox.width << "," << st.marker_bbox.height;
		for (int i=0;i<4;i++) {
			const auto& q = st.q[i];
			os << "," << (q.valid ? 1 : 0);
			if (q.valid) {
				os << "," << q.motion.pos.x << "," << q.motion.pos.y
				   << "," << q.motion.vel.x << "," << q.motion.vel.y
				   << "," << q.motion.acc.x << "," << q.motion.acc.y;
			} else {
				os << ",,,,,,"; // 7 empty fields when invalid
			}
		}
		os << "\n";
		return os.str();
	}

	void run() {
		while (running_) {
			std::unique_lock<std::mutex> lk(m_);
			cv_.wait_for(lk, std::chrono::milliseconds(100), [&]{ return !queue_.empty() || !running_; });
			while (!queue_.empty()) {
				auto line = std::move(queue_.front());
				queue_.pop();
				lk.unlock();
				if (ofs_.is_open()) ofs_ << line;
				lk.lock();
			}
		}
	}

	std::ofstream ofs_;
	std::string out_dir_;
	std::string csv_path_;

	std::mutex init_m_;
	bool initialized_ = false;

	std::thread worker_;
	std::mutex m_;
	std::condition_variable cv_;
	std::queue<std::string> queue_;
	const size_t max_queue_ = 1024; // bounded to prevent RAM growth
	std::atomic<bool> running_{false};
};
