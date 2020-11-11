#include "proc_cc.h"
#include "proc_ctx.h"
#include "../query_ctx.h"
#include "../algorithms/LAGraph_cc_fastsv.h"

// CALL algo.CC(relationshipQuery) 

typedef struct {
	GrB_Matrix M;                   // 
	const char *relationship;       // 
	SIValue *output;                // 
	bool ccProduced;                //                
} CC_Ctx;

static GrB_Matrix _BuildSymmetricMatrix(const char *relation) {
	assert(relation);
	GrB_Matrix M = GrB_NULL;
	
	GraphContext *gc = QueryCtx_GetGraphCtx();
	Schema *s = GraphContext_GetSchema(gc, relation, SCHEMA_EDGE);
	if (!s) return GrB_NULL;//relationship type doesn't exists.

	Graph *g = QueryCtx_GetGraph();
	GrB_Index n = Graph_RequiredMatrixDim(g);
	GrB_Matrix R = Graph_GetRelationMatrix(g, s->id);

	assert(GrB_Matrix_new(&M, GrB_BOOL, n, n) == GrB_SUCCESS);
	
	// Extract relations out of matrix.
	GrB_Index nz;
	assert(GrB_Matrix_nvals(&nz, R) == GrB_SUCCESS);
	GrB_Index *I = rm_malloc(sizeof(GrB_Index) * nz);
	GrB_Index *J = rm_malloc(sizeof(GrB_Index) * nz);
	uint64_t  *X = rm_malloc(sizeof(uint64_t) * nz);
	assert(GrB_Matrix_extractTuples_UINT64(I, J, X, &nz, R) == GrB_SUCCESS);
	
	// Process each relation.construct symmetric matrix
	for(GrB_Index i = 0; i < nz; i++) {
		Edge e;
		EdgeID id = X[i];
		GrB_Index row = I[i];
		GrB_Index col = J[i];
		bool exist = true;

		if(SINGLE_EDGE(id)) {
			assert(GrB_Matrix_setElement_BOOL(M, exist, row, col) == GrB_SUCCESS);
			assert(GrB_Matrix_setElement_BOOL(M, exist, col, row) == GrB_SUCCESS);
		} else {
			assert("TODO: handle multiple edges." && false);
		}
	}

	rm_free(I);
	rm_free(J);
	rm_free(X);
	return M;
}

ProcedureResult Proc_CCInvoke(ProcedureCtx *ctx, const SIValue *args) {
	if(array_len((SIValue *)args) != 1) return PROCEDURE_ERR;
	if(SI_TYPE(args[0]) != T_STRING) 
		return PROCEDURE_ERR;

	ctx->privateData = NULL;

	const char *relation = SIValue_IsNull(args[0]) ? NULL : args[0].stringval;

	// Get relation matrix.
	GrB_Matrix M = GrB_NULL;
	GraphContext *gc = QueryCtx_GetGraphCtx();
	if(relation == NULL) 
		M = Graph_GetAdjacencyMatrix(gc->g);
	else 
		M = _BuildSymmetricMatrix(relation);
	if (M == GrB_Matrix) return PROCEDURE_ERR;
	
	// Setup context.
	CC_Ctx *pdata = rm_malloc(sizeof(CC_Ctx));
	pdata->M = M;
	pdata->relationship = relation;
	pdata->output = array_new(SIValue, 4);
	pdata->output = array_append(pdata->output, SI_ConstStringVal("Nodeidx"));
	pdata->output = array_append(pdata->output, SI_NullVal()); // Place holder.
	pdata->output = array_append(pdata->output, SI_ConstStringVal("value"));
	pdata->output = array_append(pdata->output, SI_NullVal()); // Place holder.
	pdata->ccProduced = false;
	
	ctx->privateData = pdata;
	return PROCEDURE_OK;
}

SIValue *Proc_CCStep(ProcedureCtx *ctx) {
	assert(ctx->privateData);

	SIValue *res = NULL;
	CC_Ctx *pdata = (CC_Ctx *)ctx->privateData;
	GrB_Vector cc = GrB_NULL;//pointer to the return vector  

	if(pdata->ccProduced) goto cleanup;
	// Mark this call to Step, such that additional calls will return NULL.
	pdata->ccProduced = true;

	assert(LAGraph_cc_fastsv(&cc, pdata->M, true) == GrB_SUCCESS);

	//Get number of entries in cc algorithm output
	GrB_Index nvals;
	assert(GrB_Vector_nvals(&nvals, cc) == GrB_SUCCESS);
	SIValue nodes = SI_Array(nvals);
	SIValue values = SI_Array(nvals);

	//set result
	GrB_Index *node_idx = rm_malloc(nvals * sizeof(GrB_Index));
	uint64_t *value = rm_malloc(nvals * sizeof(uint64_t));
	assert(GrB_Vector_extractTuples_UINT64(node_idx, value, &nvals, cc) == GrB_SUCCESS);
	for (GrB_Index i = 0; i < nvals; i++) {
		SIArray_Append(&nodes, SI_LongVal(node_idx[i]));
		SIArray_Append(&values, SI_LongVal(value[i]));
		//printf("%ld : node = %ld, value = %d\n", i, node_id, value[i]);
	}	
	pdata->output[1] = nodes;
	pdata->output[3] = values;

	pdata->ccProduced = true; // Mark that this node has been mapped.
	return pdata->output;
}

ProcedureResult Proc_CCFree(ProcedureCtx *ctx) {
	// Clean up.
	if(ctx->privateData) {
		CC_Ctx *pdata = ctx->privateData;
		GrB_free(&pdata->M);
		array_free(pdata->output);
		rm_free(ctx->privateData);
	}

	return PROCEDURE_OK;
}

ProcedureCtx *Proc_CCCtx() {
    void *privateData = NULL;
	ProcedureOutput **outputs = array_new(ProcedureOutput *, 2);

	ProcedureOutput *output_idx = rm_malloc(sizeof(ProcedureOutput));
	output_cost->name = "Nodeidx";
	output_cost->type = T_ARRAY;
	ProcedureOutput *output_val = rm_malloc(sizeof(ProcedureOutput));
	output_cost->name = "value";
	output_cost->type = T_ARRAY;

	outputs = array_append(outputs, output_idx);
	outputs = array_append(outputs, output_val);

	unsigned int ninputs = 1;    // Start node, end node, weight property, edge type, default weight.
	ProcedureCtx *ctx = ProcCtxNew("algo.CC",
								   ninputs,
								   outputs,
								   Proc_CCStep,
								   Proc_CCInvoke,
								   Proc_CCFree,
								   privateData,
								   true);
	return ctx;
}