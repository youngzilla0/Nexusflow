#ifndef NEXUSFLOW_ERRORCODE_HPP
#define NEXUSFLOW_ERRORCODE_HPP

namespace nexusflow {

enum ErrorCode {
    SUCCESS = 0,
    FAILURE = SUCCESS + 1,
    FAILED_ALREADY_START,
    FAILED_ALREADY_STOP,
    FAILED_TO_START_WORKER,
    FAILED_TO_STOP_WORKER,
    UNINITIALIZED_ERROR,
};

} // namespace nexusflow

#endif