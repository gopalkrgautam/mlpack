/** @file nwrcde.h
 *
 *  This file contains an implementation of Nadaraya-Watson regression
 *  and conditional density estimation for a linkable library
 *  component. It implements a rudimentary depth-first dual-tree
 *  algorithm with finite difference and series-expansion
 *  approximations, using the formalized GNP framework by Ryan and
 *  Garry.
 *
 *  @author Dongryeol Lee (dongryel@cc.gatech.edu)
 *  @see nwrcde_main.cc
 *  @bug No known bugs.
 */

#ifndef NWRCDE_H
#define NWRCDE_H

#define INSIDE_NWRCDE_H

#include "fastlib/fastlib.h"
#include "mlpack/series_expansion/farfield_expansion.h"
#include "mlpack/series_expansion/local_expansion.h"
#include "mlpack/series_expansion/mult_farfield_expansion.h"
#include "mlpack/series_expansion/mult_local_expansion.h"
#include "mlpack/series_expansion/kernel_aux.h"
#include "contrib/dongryel/proximity_project/gen_metric_tree.h"
#include "contrib/dongryel/proximity_project/subspace_stat.h"
#include "nwrcde_common.h"
#include "nwrcde_delta.h"
#include "nwrcde_query_postponed.h"
#include "nwrcde_query_result.h"
#include "nwrcde_query_summary.h"
#include "nwrcde_global.h"

////////// Documentation stuffs //////////
const fx_entry_doc nwrcde_main_entries[] = {
  {"data", FX_REQUIRED, FX_STR, NULL,
   "  A file containing reference data.\n"},
  {"dtarget", FX_REQUIRED, FX_STR, NULL,
   "  A file containing reference target training values.\n"},
  {"query", FX_PARAM, FX_STR, NULL,
   "  A file containing query data (defaults to data).\n"},
  FX_ENTRY_DOC_DONE
};

const fx_entry_doc nwrcde_entries[] = {
  {"bandwidth", FX_PARAM, FX_DOUBLE, NULL,
   "  The bandwidth parameter.\n"},
  {"coverage_percentile", FX_PARAM, FX_DOUBLE, NULL,
   "  The upper percentile of the estimates for the error guarantee.\n"},
  {"do_naive", FX_PARAM, FX_BOOL, NULL,
   "  Whether to perform naive computation as well.\n"},
  {"output", FX_PARAM, FX_STR, NULL,
   "  A file to receive the results of computation.\n"},
  {"kernel", FX_PARAM, FX_STR, NULL,
   "  The type of kernel to use.\n"},
  {"knn", FX_PARAM, FX_INT, NULL,
   "  The number of k-nearest neighbor to use for variable bandwidth.\n"},
  {"loo", FX_PARAM, FX_BOOL, NULL,
   "  Whether to output the density estimates using leave-one-out.\n"},
  {"mode", FX_PARAM, FX_STR, NULL,
   "  Fixed bandwidth or variable bandwidth mode.\n"},
  {"multiplicative_expansion", FX_PARAM, FX_BOOL, NULL,
   "  Whether to do O(p^D) kernel expansion instead of O(D^p).\n"},
  {"probability", FX_PARAM, FX_DOUBLE, NULL,
   "  The probability guarantee that the relative error accuracy holds.\n"},
  {"relative_error", FX_PARAM, FX_DOUBLE, NULL,
   "  The required relative error accuracy.\n"},
  {"threshold", FX_PARAM, FX_DOUBLE, NULL,
   "  If less than this value, then absolute error bound.\n"},
  {"scaling", FX_PARAM, FX_STR, NULL,
   "  The scaling option.\n"},
  FX_ENTRY_DOC_DONE
};

const fx_module_doc nwrcde_doc = {
  nwrcde_entries, NULL,
  "Performs dual-tree kernel density estimate computation.\n"
};

const fx_submodule_doc nwrcde_main_submodules[] = {
  {"nwrcde", &nwrcde_doc,
   "  Responsible for Nadaraya-Watson regression and conditional density estimate computation.\n"},
  FX_SUBMODULE_DOC_DONE
};

const fx_module_doc nwrcde_main_doc = {
  nwrcde_main_entries, nwrcde_main_submodules,
  "This is the driver for the kernel density estimator.\n"
};


template<typename TKernel>
class NWRCde {

 public:
    
  /** @brief The type of our query tree.
   */
  typedef GeneralBinarySpaceTree<DBallBound < LMetric<2>, Vector >, Matrix > QueryTree;

  /** @brief The type of our reference tree.
   */
  typedef GeneralBinarySpaceTree<DBallBound < LMetric<2>, Vector >, Matrix > ReferenceTree;

 private:

  ////////// Private Constants //////////

  ////////// Private Member Variables //////////

  /** @brief The list of parameters.
   */
  NWRCdeGlobal<TKernel, ReferenceTree> parameters_;


  ////////// Private Member Functions //////////

  /** @brief The exhaustive base Nadaraya-Watson regression and
   *         conditional density estimation.
   */
  void NWRCdeBase_(const Matrix &qset, QueryTree *qnode, ReferenceTree *rnode, 
		   double probability, NWRCdeQueryResult &query_results);
  
  double EvalUnnormOnSq_(index_t reference_point_index,
			 double squared_distance);

  /** @brief Canonical dual-tree Nadaraya-Watson regression and
   *         conditional density estimation.
   *
   *  @param qnode The query node.
   *  @param rnode The reference node.
   *  @param probability The required probability; 1 for exact
   *         approximation.
   *
   *  @return true if the entire contribution of rnode has been
   *          approximated using an exact method, false otherwise.
   */
  bool NWRCdeCanonical_(const Matrix &qset, QueryTree *qnode, 
			ReferenceTree *rnode, double probability, 
			NWRCdeQueryResult &query_results);

  void PreProcessQueryTree_(QueryTree *node);

  /** @brief Pre-processing step - this wouldn't be necessary if the
   *         core fastlib supported a Init function for Stat objects
   *         that take more arguments.
   */
  void PreProcessReferenceTree_(ReferenceTree *node);

  /** @brief Post processing step.
   */
  void PostProcessQueryTree_(QueryTree *qnode, 
			     NWRCdeQueryResult &query_results);

 public:

  ////////// Constructor/Destructor //////////

  /** @brief The default constructor.
   */
  NWRCde() {
    parameters_.rroot = NULL;
  }

  /** @brief The default destructor which deletes the trees.
   */
  ~NWRCde() {
    delete parameters_.rroot;
  }

  ////////// User Level Functions //////////

  void Compute(const Matrix &queries, NWRCdeQueryResult *query_results) {

    index_t leaflen = fx_param_int(parameters_.module, "leaflen", 20);
    double probability = 
      fx_param_double(parameters_.module, "probability", 1.0);

    // Make a copy of the query set.
    Matrix qset;
    qset.Copy(queries);

    // Initialize the temporary sum accumulators to zero.    
    query_results->Init();

    // Build the query tree.
    ArrayList<index_t> old_from_new_queries;
    QueryTree *qroot = proximity::MakeGenMetricTree<QueryTree>
      (qset, leaflen, &old_from_new_queries, NULL);

    // Compute the estimates using a dual-tree based algorithm.
    PreProcessQueryTree_(qroot);
    NWRCdeCanonical_(qset, qroot, parameters_.rroot, probability, 
		     *query_results);
    PostProcessQueryTree_(qroot, *query_results);
  }

  void Init(const Matrix &references, const Matrix &reference_targets,
	    struct datanode *module_in) {

    // Set the module pointer.
    parameters_.module = module_in;

    // Construct the reference tree.
    int leaflen = fx_param_int(module_in, "leaflen", 20);

    // Copy the reference dataset and construct the reference tree.
    parameters_.rset_.Copy(references);
    DEBUG_ASSERT(references.n_cols() == reference_targets.n_cols());
    parameters_.rset_targets_.Init(reference_targets.n_cols());
    parameters_.rset_target_sum_ = 0;
    for(index_t i = 0; i < parameters_.rset_targets_.length(); i++) {
      parameters_.rset_targets_[i] = reference_targets.get(0, i);
      parameters_.rset_target_sum_ += parameters_.rset_targets_[i];
    }
    
    fx_timer_start(parameters_.module, "reference_tree_construct");
    parameters_.rroot = proximity::MakeGenMetricTree<ReferenceTree>
      (parameters_.rset, leaflen, &(parameters_.old_from_new_references),
       NULL);
    fx_timer_stop(parameters_.module, "reference_tree_construct");
    
    // Initialize the kernel.
    double bandwidth = fx_param_double_req(parameters_.module, "bandwidth");
    parameters_.kernel_.Init(parameters_.bandwidth);
  }

  void PrintDebug();

};

#include "nwrcde_impl.h"
#undef INSIDE_NWRCDE_H

#endif
