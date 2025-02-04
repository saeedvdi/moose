//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#pragma once

#include "MooseTypes.h"
#include "libmesh/nanoflann.hpp"
#include <memory>
#include <vector>
#include <tuple>

/**
 * ValueCache is a generic helper template to implement an unstructured data
 * cache, where arbitrary result types can be placed in an n-dimensional space of
 * real numbers. Upon lookup the closest result to a given point in n-space is
 * returned. Applications include caching reasonable initial guesses for local
 * Newton solves as found in certain phase field models as well as thermodynamic
 * Gibbs energy minimization.
 */
template <typename T>
class ValueCache
{
public:
  ValueCache(std::size_t in_dim, std::size_t max_leaf_size = 10);

  void insert(const std::vector<Real> & in_val, const T & out_val);
  std::size_t size();
  bool guess(const std::vector<Real> & in_val, T & out_val, Real & distance_sqr);
  std::vector<std::tuple<std::vector<Real> &, T &, Real>>
  kNearestNeighbors(const std::vector<Real> & in_val, const std::size_t k);

protected:
  struct PointCloud
  {
    PointCloud(std::size_t in_dim) : _in_dim(in_dim) {}

    std::vector<std::vector<Real>> _pts;
    const std::size_t _in_dim;

    inline size_t kdtree_get_point_count() const { return _pts.size(); }
    inline Real kdtree_get_pt(const std::size_t idx, const std::size_t dim) const
    {
      return _pts[idx][dim];
    }
    template <class BBOX>
    bool kdtree_get_bbox(BBOX & /* bb */) const
    {
      return false;
    }
  } _point_cloud;

  using KdTreeT =
      nanoflann::KDTreeSingleIndexDynamicAdaptor<nanoflann::L2_Simple_Adaptor<Real, PointCloud>,
                                                 PointCloud,
                                                 -1,
                                                 std::size_t>;

  std::unique_ptr<KdTreeT> _kd_tree;
  std::vector<T> _data;

  const std::size_t _in_dim;
  const std::size_t _max_leaf_size;
  const std::size_t _max_subtrees;
};

template <typename T>
ValueCache<T>::ValueCache(std::size_t in_dim, std::size_t max_leaf_size)
  : _point_cloud(in_dim),
    _kd_tree(nullptr),
    _in_dim(in_dim),
    _max_leaf_size(max_leaf_size),
    _max_subtrees(100)
{
}

template <typename T>
void
ValueCache<T>::insert(const std::vector<Real> & in_val, const T & out_val)
{
  mooseAssert(in_val.size() == _in_dim, "Key dimensions do not match cache dimensions");

  auto id = _point_cloud._pts.size();
  mooseAssert(size() == id, "Inconsistent cache data size.");

  _point_cloud._pts.push_back(in_val);
  _data.push_back(out_val);

  // do we have too many subtrees?
  if (_kd_tree && _kd_tree->getAllIndices().size() > _max_subtrees)
    _kd_tree = nullptr;

  // rebuild tree or add point
  if (!_kd_tree)
  {
    _kd_tree = std::make_unique<KdTreeT>(_in_dim, _point_cloud, _max_leaf_size);
    mooseAssert(_kd_tree != nullptr, "KDTree was not properly initialized.");
  }
  else
    _kd_tree->addPoints(id, id);
}

template <typename T>
std::size_t
ValueCache<T>::size()
{
  return _data.size();
}

template <typename T>
bool
ValueCache<T>::guess(const std::vector<Real> & in_val, T & out_val, Real & distance_sqr)
{
  // cache is empty
  if (_data.empty())
    return false;

  nanoflann::KNNResultSet<Real> result_set(1);
  std::size_t return_index;
  result_set.init(&return_index, &distance_sqr);

  // perform search
  _kd_tree->findNeighbors(result_set, in_val.data());

  // no result found
  if (result_set.size() != 1)
    return false;

  out_val = _data[return_index];
  return true;
}

/**
 * This function performs a search on the value cache and returns either the k-nearest neighbors or
 * the neighbors available if the cache size is less than k.
 */
template <typename T>
std::vector<std::tuple<std::vector<Real> &, T &, Real>>
ValueCache<T>::kNearestNeighbors(const std::vector<Real> & in_val, const std::size_t k)
{
  std::vector<std::tuple<std::vector<Real> &, T &, Real>> nearest_neighbors;

  nanoflann::KNNResultSet<Real> result_set(std::min(k, size()));
  std::vector<std::size_t> return_indices(std::min(k, size()));
  std::vector<Real> distances(std::min(k, size()));

  result_set.init(return_indices.data(), distances.data());

  // kNN search
  _kd_tree->findNeighbors(result_set, in_val.data());

  for (std::size_t i = 0; i < result_set.size(); ++i)
    nearest_neighbors.push_back(
        std::tie((_point_cloud._pts[return_indices[i]]), (_data[return_indices[i]]), distances[i]));

  return nearest_neighbors;
}
