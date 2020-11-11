#pragma once

#include "../../deps/GraphBLAS/Include/GraphBLAS.h"

GrB_Info LAGraph_cc_fastsv
(
    GrB_Vector *result,     // output: array of component identifiers
    GrB_Matrix A,           // input matrix
    bool sanitize           // if true, ensure A is symmetric
)
