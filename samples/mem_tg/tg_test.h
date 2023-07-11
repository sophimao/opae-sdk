// Copyright(c) 2022, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <unistd.h>
#include <thread>
#include <iostream>
#include <vector>
#include <future>

#include "afu_test.h"
#include "mem_tg.h"

using test_afu = opae::afu_test::afu;
using opae::fpga::types::token;

namespace mem_tg {

class tg_test : public test_command
{
public:
    tg_test()
        :tg_exe_(NULL) {
          tg_offset_ = 0x0;
    }
    virtual ~tg_test(){}

    virtual  const char *name() const override
    {
        return "tg_test";
    }

    virtual  const char *description() const override
    {
      return "configure & run mem traffic generator test";
    }

    virtual const char *afu_id() const override
    {
      return AFU_ID;
    }


    // Convert number of transactions to bandwidth (GB/s)
    double bw_calc(uint64_t xfer_bytes, uint64_t num_ticks)
    {
        return (double)(xfer_bytes) / ((1000.0 / (double)tg_exe_->mem_speed_ * (double)num_ticks));
    }

    void tg_perf (mem_tg *tg_exe_) {
      uint32_t mem_ch_offset = (std::stoi(tg_exe_->mem_ch_[0])) << 0x3;
      uint64_t num_ticks = tg_exe_->read64(MEM_TG_CLOCKS + mem_ch_offset);
      std::cout << "Mem Clock Cycles: " << std::dec << num_ticks << std::endl;
      uint64_t write_bytes = 64 * (tg_exe_->loop_*tg_exe_->wcnt_*tg_exe_->bcnt_);
      uint64_t read_bytes  = 64 * (tg_exe_->loop_*tg_exe_->rcnt_*tg_exe_->bcnt_);

      std::cout << "Write BW: " << bw_calc(write_bytes,num_ticks) << " GB/s" << std::endl;
      std::cout << "Read BW: "  << bw_calc(read_bytes,num_ticks)  << " GB/s\n" << std::endl;
    }
  
    bool tg_wait_test_completion (mem_tg *tg_exe_)
    {
        /* Wait for test completion */
        uint32_t           timeout = MEM_TG_TEST_TIMEOUT;
        // poll while active bit is set (channel status = {pass,fail,timeout,active})
        uint32_t tg_status = 0x1;
        tg_status = 0xF&(tg_exe_->read64(MEM_TG_STAT) >> (0x4*(std::stoi(tg_exe_->mem_ch_[0]))));
        while ( tg_status == TG_STATUS_ACTIVE ) {
          tg_status = 0xF&(tg_exe_->read64(MEM_TG_STAT) >> (0x4*(std::stoi(tg_exe_->mem_ch_[0]))));
          // usleep(TEST_SLEEP_INVL);
	        std::this_thread::yield();
          if (--timeout == 0) {
            std::cout << "TG TEST TIME OUT" << std::endl;
            return false;
          }

        }
	      std::cout << "Channel " << std::stoi(tg_exe_->mem_ch_[0]) << ":" << std::endl;
      	if(tg_status == TG_STATUS_TIMEOUT) {
          std::cout << "TG TIMEOUT" << std::endl;
          return false;
        }
        uint32_t tg_fail_exp  = 0;
        uint32_t tg_fail_act  = 0;
        uint64_t tg_fail_addr = 0;

        if (tg_status == TG_STATUS_ERROR) {
          std::cout << "TG ERROR" << std::endl;
          tg_fail_addr = tg_exe_->read64(tg_offset_ + TG_FIRST_FAIL_ADDR_L);
          tg_fail_exp  = tg_exe_->read64(tg_offset_ + TG_FAIL_EXPECTED_DATA);
          tg_fail_act  = tg_exe_->read64(tg_offset_ + TG_FAIL_READ_DATA);
          std::cout << "Failed at address 0x" << std::hex << tg_fail_addr << " exp=0x" << tg_fail_exp << " act=0x" << tg_fail_act << std::endl;
          return false;
        }
        std::cout << "TG PASS" << std::endl;
        return true;
    }

    int config_input_options(mem_tg *tg_exe_)
    {
	    if (!tg_exe_)
        return -1;
	    uint64_t mem_capability = tg_exe_->read64(MEM_TG_CTRL);
    	if((mem_capability & (0x1 << std::stoi(tg_exe_->mem_ch_[0]))) == 0) { 
          std::cerr << "No traffic generator for mem[" << std::stoi(tg_exe_->mem_ch_[0]) << "]" << std::endl;
          return -1;
        } else {
          tg_offset_ = AFU_DFH + (MEM_TG_CFG_OFFSET * (1+std::stoi(tg_exe_->mem_ch_[0])));
        }
        tg_exe_->write32(tg_offset_+TG_LOOP_COUNT,  tg_exe_->loop_);
        tg_exe_->write32(tg_offset_+TG_WRITE_COUNT, tg_exe_->wcnt_);
        tg_exe_->write32(tg_offset_+TG_READ_COUNT,  tg_exe_->rcnt_);
        tg_exe_->write32(tg_offset_+TG_BURST_LENGTH, tg_exe_->bcnt_);
        tg_exe_->write32(tg_offset_+TG_SEQ_ADDR_INCR, tg_exe_->stride_);
        tg_exe_->write32(tg_offset_+TG_PPPG_SEL, tg_exe_->pattern_);

        // address increment mode
        tg_exe_->write32(tg_offset_+TG_ADDR_MODE_WR, TG_ADDR_SEQ);
        tg_exe_->write32(tg_offset_+TG_ADDR_MODE_RD, TG_ADDR_SEQ);
        return 0;
    }

    // The test state has been configured. Run one test instance.
    int run_mem_test(mem_tg *tg_exe_)
    {
	      int status = 0;
	
        tg_exe_->logger_->debug("Start Test");

        tg_exe_->write32(tg_offset_+TG_START,0x1);

        if(!tg_wait_test_completion(tg_exe_))
          status = -1;

        tg_perf(tg_exe_);
	
        return status;
    }

    int run_thread_single_channel(mem_tg *tg_exe_){
	      auto ret = config_input_options(tg_exe_);
        if (ret != 0) {
            std::cerr << "Failed to configure TG input options" << std::endl;
            return ret;
        }
        int status = run_mem_test(tg_exe_);
        return status;
    }

    virtual int run(test_afu *afu, CLI::App *app) override
    {
        (void)app;
        auto d_afu = dynamic_cast<mem_tg*>(afu);
        tg_exe_ = dynamic_cast<mem_tg*>(afu);

        token_ = d_afu->get_token();

        // Read HW details

        if (0 == tg_exe_->mem_speed_) {
          tg_exe_->mem_speed_ = 300;
          std::cout << "Memory channel clock frequency unknown. Assuming "
              << tg_exe_->mem_speed_ << " MHz." << std::endl;
        }
        else {
            std::cout << "Memory clock from command line: "
                      << tg_exe_->mem_speed_ << " MHz" << std::endl;
        }

        // Parse mem_ch_ into array of selected channels and number of channels
	int *channels = NULL;
        int num_channels = 0;
        if (0 < (tg_exe_->mem_ch_).size()) {
          if ((tg_exe_->mem_ch_[0]).find("all") == 0){	
            uint64_t mem_capability = tg_exe_->read64(MEM_TG_CTRL);
            channels = new int[sizeof(uint64_t)]; // size should be same as mem_capability 
            int last = 0;
            for (uint32_t i = 0; i < sizeof(uint64_t); i++){  // number of itterations should be same as mem_capability 
              if ((mem_capability & (1ULL << i)) != 0){
                channels[last] = i;
                last += 1;
              }
            }
            channels[last] = -1; // EOL
            num_channels = last;
          } else {
            channels = new int[tg_exe_->mem_ch_.size()];
            num_channels = tg_exe_->mem_ch_.size();
            try{
              for (unsigned i = 0; i < tg_exe_->mem_ch_.size(); i++){
                channels[i] = std::stoi(tg_exe_->mem_ch_[i]);
              }
            } catch (std::invalid_argument& e){
              std::cerr << "Error: invalid argument to std::stoi";
              delete[] channels;
              return 1;
            }
          }
        } else {
	        std::cout << "Insufficient arguments provided" << std::endl;
	        exit(1);
	      }

        // Spawn threads for each channel:
        mem_tg *thread_tg_exe_objects[num_channels];
	      std::vector<std::future<int>> futures;
        std::vector<std::promise<int>> promises(num_channels);
        std::vector<std::thread> threads;
        for (int i = 0; i < num_channels; i++){ 
          if (channels[i] == -1) break;
	        thread_tg_exe_objects[i] = new mem_tg;
          tg_exe_->duplicate(thread_tg_exe_objects[i]);
          thread_tg_exe_objects[i]->mem_ch_.clear();
          thread_tg_exe_objects[i]->mem_ch_.push_back(std::to_string(channels[i]));
          futures.push_back(promises[i].get_future());
          threads.emplace_back([&, i] { 
			      promises[i].set_value(run_thread_single_channel(thread_tg_exe_objects[i])); 
	        });
        }
        // Wait for all threads to exit then collect their exit statuses
        for (auto &thread : threads){
	        thread.join();
        }
        std::vector<int> exit_codes;
        for (auto& future : futures){
          exit_codes.push_back(future.get());
        }

        // Print message showing thread statuses
        for (int i = 0; i < num_channels; i++){
          std::cout << "Thread on channel " << channels[i] << " exited with status " << (long)exit_codes[i] << std::endl;
        }

        // Delete dynamic allocations 
        delete[] channels;
        for (int i = 0; i < num_channels; i++){
		      delete thread_tg_exe_objects[i];
	      }
	      return 0;
    }

protected:
    uint64_t tg_offset_;
    mem_tg *tg_exe_;
    token::ptr_t token_;
};

} // end of namespace mem_tg

