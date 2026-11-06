#pragma once
#include "anomaly/sdk/base.h"

// Anomaly 2.6.0 移除了 anomaly.ue5.functions 与 anomaly.nte.damage-chain 两个服务。
// 这里保留旧 ABI 定义仅用于编译：运行时 Query 返回空，依赖它们的实验功能自动不启用；
// 时停、UI、输入等不依赖这两个服务的能力保持原样。
#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnomalyUe5FunctionsServiceV1 {
    uint32_t struct_size;
    uint32_t service_version;
    void* user;
    AnomalyStatusV1 (ANOMALY_CALL *invoke)(
        void* user, uintptr_t object, uintptr_t function,
        void* parameters, size_t parameters_size);
} AnomalyUe5FunctionsServiceV1;

typedef struct AnomalyNteDamageChainServiceV1 {
    uint32_t struct_size;
    uint32_t service_version;
    void* user;
    AnomalyStatusV1 (ANOMALY_CALL *apply_address)(void* user, uintptr_t* address);
    AnomalyStatusV1 (ANOMALY_CALL *gate_address)(void* user, uintptr_t* address);
} AnomalyNteDamageChainServiceV1;

#ifdef __cplusplus
}
#endif
