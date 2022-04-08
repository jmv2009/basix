// Copyright (c) 2021 Chris Richardson & Matthew Scroggs
// FEniCS Project
// SPDX-License-Identifier:    MIT

#include "e-serendipity.h"
#include "e-lagrange.h"
#include "element-families.h"
#include "maps.h"
#include "moments.h"
#include "polyset.h"
#include "quadrature.h"
#include <xtensor/xadapt.hpp>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xtensor.hpp>
#include <xtensor/xview.hpp>

using namespace basix;

namespace
{
//----------------------------------------------------------------------------
xt::xtensor<double, 2> make_serendipity_space_2d(int degree)
{
  const std::size_t ndofs = degree == 1 ? 4 : degree * (degree + 3) / 2 + 3;

  // Evaluate the expansion polynomials at the quadrature points
  auto [pts, _wts] = quadrature::make_quadrature(
      quadrature::type::Default, cell::type::quadrilateral, 2 * degree);
  auto wts = xt::adapt(_wts);

  xt::xtensor<double, 2> Pq
      = xt::view(polyset::tabulate(cell::type::quadrilateral, degree, 0, pts),
                 0, xt::all(), xt::all());

  const std::size_t psize = Pq.shape(1);

  // Create coefficients for order (degree) polynomials
  xt::xtensor<double, 2> wcoeffs = xt::zeros<double>({ndofs, psize});
  int row_n = 0;
  for (int i = 0; i <= degree; ++i)
    for (int j = 0; j <= degree - i; ++j)
      wcoeffs(row_n++, i * (degree + 1) + j) = 1;

  auto q0 = xt::col(pts, 0);
  auto q1 = xt::col(pts, 1);
  if (degree == 1)
  {
    for (std::size_t k = 0; k < psize; ++k)
      wcoeffs(row_n, k) = xt::sum(wts * q0 * q1 * xt::col(Pq, k))();
    return wcoeffs;
  }

  xt::xtensor<double, 1> integrand;
  for (std::size_t k = 0; k < psize; ++k)
  {
    auto pk = xt::col(Pq, k);
    for (std::size_t a = 0; a < 2; ++a)
    {
      auto q_a = xt::col(pts, a);
      integrand = wts * q0 * q1 * pk;
      for (int i = 1; i < degree; ++i)
        integrand *= q_a;
      wcoeffs(row_n + a, k) = xt::sum(integrand)();
    }
  }

  return wcoeffs;
}
//----------------------------------------------------------------------------
std::vector<std::array<int, 3>>
serendipity_3d_indices(int total, int linear, std::vector<int> done = {})
{
  if (done.size() == 3)
  {
    int count = 0;
    for (int i = 0; i < 3; ++i)
      if (done[i] == 1)
        ++count;

    if (count >= linear)
      return {{done[0], done[1], done[2]}};
    return {};
  }
  else if (done.size() == 2)
  {
    return serendipity_3d_indices(
        total, linear, {done[0], done[1], total - done[0] - done[1]});
  }

  std::vector<int> new_done(done.size() + 1);
  int sum_done = 0;
  for (std::size_t i = 0; i < done.size(); ++i)
  {
    new_done[i] = done[i];
    sum_done += done[i];
  }

  std::vector<std::array<int, 3>> out;
  for (int i = 0; i <= total - sum_done; ++i)
  {
    new_done[done.size()] = i;
    for (std::array<int, 3> j : serendipity_3d_indices(total, linear, new_done))
      out.push_back(j);
  }

  return out;
}
//----------------------------------------------------------------------------
xt::xtensor<double, 2> make_serendipity_space_3d(int degree)
{
  const std::size_t ndofs
      = degree == 0
            ? 1
            : (degree < 4
                   ? 12 * degree - 4
                   : (degree < 6 ? 3 * degree * degree - 3 * degree + 14
                                 : degree * (degree - 1) * (degree + 1) / 6
                                       + degree * degree + 5 * degree + 4));
  // Number of order (degree) polynomials

  // Evaluate the expansion polynomials at the quadrature points
  auto [pts, _wts] = quadrature::make_quadrature(
      quadrature::type::Default, cell::type::hexahedron, 2 * degree);
  auto wts = xt::adapt(_wts);
  xt::xtensor<double, 2> Ph
      = xt::view(polyset::tabulate(cell::type::hexahedron, degree, 0, pts), 0,
                 xt::all(), xt::all());

  const std::size_t psize = Ph.shape(1);

  // Create coefficients for order (degree) polynomials
  xt::xtensor<double, 2> wcoeffs = xt::zeros<double>({ndofs, psize});
  int row_n = 0;
  for (int i = 0; i <= degree; ++i)
    for (int j = 0; j <= degree - i; ++j)
      for (int k = 0; k <= degree - i - j; ++k)
        wcoeffs(row_n++, i * (degree + 1) * (degree + 1) + j * (degree + 1) + k)
            = 1;

  xt::xtensor<double, 1> integrand;
  std::vector<std::array<int, 3>> indices;
  for (std::size_t s = 1; s <= 3; ++s)
  {
    indices = serendipity_3d_indices(s + degree, s);
    for (std::array<int, 3> i : indices)
    {
      for (std::size_t k = 0; k < psize; ++k)
      {
        integrand = wts * xt::col(Ph, k);
        for (int d = 0; d < 3; ++d)
        {
          auto q_d = xt::col(pts, d);
          for (int j = 0; j < i[d]; ++j)
            integrand *= q_d;
        }

        wcoeffs(row_n, k) = xt::sum(integrand)();
      }
      ++row_n;
    }
  }

  return wcoeffs;
}
//----------------------------------------------------------------------------
xt::xtensor<double, 2> make_serendipity_div_space_2d(int degree)
{
  const std::size_t ndofs = degree * (degree + 3) + 4;

  // Evaluate the expansion polynomials at the quadrature points
  auto [pts, _wts] = quadrature::make_quadrature(
      quadrature::type::Default, cell::type::quadrilateral, 2 * degree + 2);
  auto wts = xt::adapt(_wts);

  xt::xtensor<double, 2> Pq = xt::view(
      polyset::tabulate(cell::type::quadrilateral, degree + 1, 0, pts), 0,
      xt::all(), xt::all());

  const std::size_t psize = Pq.shape(1);
  const std::size_t nv = polyset::dim(cell::type::triangle, degree);

  // Create coefficients for order (degree) vector polynomials
  xt::xtensor<double, 2> wcoeffs = xt::zeros<double>({ndofs, psize * 2});
  int row_n = 0;
  for (int i = 0; i <= degree; ++i)
  {
    for (int j = 0; j <= degree - i; ++j)
    {
      for (int d = 0; d < 2; ++d)
      {
        wcoeffs(row_n++, d * psize + i * (degree + 2) + j) = 1;
      }
    }
  }

  auto q0 = xt::col(pts, 0);
  auto q1 = xt::col(pts, 1);
  xt::xtensor<double, 1> integrand;
  for (std::size_t k = 0; k < psize; ++k)
  {
    auto pk = xt::col(Pq, k);
    for (std::size_t d = 0; d < 2; ++d)
    {
      for (std::size_t a = 0; a < 2; ++a)
      {
        auto q_a = xt::col(pts, a);
        integrand = wts * pk;
        if (a == 0 and d == 0)
          integrand *= q0;
        else if (a == 0 and d == 1)
          integrand *= (degree + 1) * q1;
        else if (a == 1 and d == 0)
          integrand *= (degree + 1) * q0;
        else if (a == 1 and d == 1)
          integrand *= q1;

        for (int i = 0; i < degree; ++i)
          integrand *= q_a;
        wcoeffs(2 * nv + a, psize * d + k) = xt::sum(integrand)();
      }
    }
  }

  return wcoeffs;
}
//----------------------------------------------------------------------------
xt::xtensor<double, 2> make_serendipity_div_space_3d(int degree)
{
  const std::size_t ndofs = (degree + 1) * (degree * (degree + 5) + 12) / 2;

  // Evaluate the expansion polynomials at the quadrature points
  auto [pts, _wts] = quadrature::make_quadrature(
      quadrature::type::Default, cell::type::hexahedron, 2 * degree + 2);
  auto wts = xt::adapt(_wts);

  xt::xtensor<double, 2> polyset_at_Qpts
      = xt::view(polyset::tabulate(cell::type::hexahedron, degree + 1, 0, pts),
                 0, xt::all(), xt::all());

  const std::size_t psize = polyset_at_Qpts.shape(1);
  const std::size_t nv = polyset::dim(cell::type::tetrahedron, degree);

  // Create coefficients for order (degree) vector polynomials
  xt::xtensor<double, 2> wcoeffs = xt::zeros<double>({ndofs, psize * 3});
  int row_n = 0;
  for (int i = 0; i <= degree; ++i)
  {
    for (int j = 0; j <= degree - i; ++j)
    {
      for (int k = 0; k <= degree - i - j; ++k)
      {
        for (int d = 0; d < 3; ++d)
        {
          wcoeffs(row_n++, d * psize + i * (degree + 2) * (degree + 2)
                               + j * (degree + 2) + k)
              = 1;
        }
      }
    }
  }

  auto q0 = xt::col(pts, 0);
  auto q1 = xt::col(pts, 1);
  auto q2 = xt::col(pts, 2);
  xt::xtensor<double, 1> integrand;
  for (std::size_t k = 0; k < psize; ++k)
  {
    auto pk = xt::col(polyset_at_Qpts, k);
    for (std::size_t d = 0; d < 3; ++d)
    {
      for (std::size_t a = 0; a < 3; ++a)
      {
        for (int index = 0; index <= degree; ++index)
        {
          auto q_a = xt::col(pts, a);
          integrand = wts * pk;
          if (a == 0)
          {
            if (d == 0)
              integrand *= -(degree + 2) * q0;
            else if (d == 1)
              integrand *= q1;
            else if (d == 2)
              integrand *= q2;

            for (int i = 0; i < index; ++i)
              integrand *= q1;
            for (int i = 0; i < degree - index; ++i)
              integrand *= q2;
          }
          else if (a == 1)
          {
            if (d == 0)
              integrand *= -q0;
            else if (d == 1)
              integrand *= (degree + 2) * q1;
            else if (d == 2)
              integrand *= -q2;

            for (int i = 0; i < index; ++i)
              integrand *= q0;
            for (int i = 0; i < degree - index; ++i)
              integrand *= q2;
          }
          else if (a == 2)
          {
            if (d == 0)
              integrand *= q0;
            else if (d == 1)
              integrand *= q1;
            else if (d == 2)
              integrand *= -(degree + 2) * q2;

            for (int i = 0; i < index; ++i)
              integrand *= q0;
            for (int i = 0; i < degree - index; ++i)
              integrand *= q1;
          }

          wcoeffs(3 * nv + 3 * index + a, psize * d + k) = xt::sum(integrand)();
        }
      }
    }
  }

  return wcoeffs;
}
//----------------------------------------------------------------------------
xt::xtensor<double, 2> make_serendipity_curl_space_2d(int degree)
{
  const std::size_t ndofs = degree * (degree + 3) + 4;

  // Evaluate the expansion polynomials at the quadrature points
  auto [pts, _wts] = quadrature::make_quadrature(
      quadrature::type::Default, cell::type::quadrilateral, 2 * degree + 2);
  auto wts = xt::adapt(_wts);

  xt::xtensor<double, 2> polyset_at_Qpts = xt::view(
      polyset::tabulate(cell::type::quadrilateral, degree + 1, 0, pts), 0,
      xt::all(), xt::all());

  const std::size_t psize = polyset_at_Qpts.shape(1);
  const std::size_t nv = polyset::dim(cell::type::triangle, degree);

  // Create coefficients for order (degree) vector polynomials
  xt::xtensor<double, 2> wcoeffs = xt::zeros<double>({ndofs, psize * 2});
  int row_n = 0;
  for (int i = 0; i <= degree; ++i)
  {
    for (int j = 0; j <= degree - i; ++j)
    {
      for (int d = 0; d < 2; ++d)
      {
        wcoeffs(row_n++, d * psize + i * (degree + 2) + j) = 1;
      }
    }
  }

  auto q0 = xt::col(pts, 0);
  auto q1 = xt::col(pts, 1);
  xt::xtensor<double, 1> integrand;
  for (std::size_t k = 0; k < psize; ++k)
  {
    auto pk = xt::col(polyset_at_Qpts, k);
    for (std::size_t d = 0; d < 2; ++d)
    {
      for (std::size_t a = 0; a < 2; ++a)
      {
        auto q_a = xt::col(pts, a);
        integrand = wts * pk;
        if (a == 0 and d == 0)
          integrand *= (degree + 1) * q1;
        else if (a == 0 and d == 1)
          integrand *= -q0;
        else if (a == 1 and d == 0)
          integrand *= q1;
        else if (a == 1 and d == 1)
          integrand *= -(degree + 1) * q0;

        for (int i = 0; i < degree; ++i)
          integrand *= q_a;
        wcoeffs(2 * nv + a, psize * d + k) = xt::sum(integrand)();
      }
    }
  }

  return wcoeffs;
}
//----------------------------------------------------------------------------
xt::xtensor<double, 2> make_serendipity_curl_space_3d(int degree)
{
  const std::size_t ndofs = degree <= 3
                                ? 6 * (degree * (degree + 1) + 2)
                                : degree * (degree + 1) * (degree - 1) / 2
                                      + 3 * (degree * (degree + 4) + 3);

  // Evaluate the expansion polynomials at the quadrature points
  auto [pts, _wts] = quadrature::make_quadrature(
      quadrature::type::Default, cell::type::hexahedron, 2 * degree + 2);
  auto wts = xt::adapt(_wts);

  xt::xtensor<double, 2> polyset_at_Qpts
      = xt::view(polyset::tabulate(cell::type::hexahedron, degree + 1, 0, pts),
                 0, xt::all(), xt::all());

  const std::size_t psize = polyset_at_Qpts.shape(1);
  const std::size_t nv = polyset::dim(cell::type::tetrahedron, degree);

  // Create coefficients for order (degree) vector polynomials
  xt::xtensor<double, 2> wcoeffs = xt::zeros<double>({ndofs, psize * 3});
  int row_n = 0;
  for (int i = 0; i <= degree; ++i)
  {
    for (int j = 0; j <= degree - i; ++j)
    {
      for (int k = 0; k <= degree - i - j; ++k)
      {
        for (int d = 0; d < 3; ++d)
        {
          wcoeffs(row_n++, d * psize + i * (degree + 2) * (degree + 2)
                               + j * (degree + 2) + k)
              = 1;
        }
      }
    }
  }

  auto q0 = xt::col(pts, 0);
  auto q1 = xt::col(pts, 1);
  auto q2 = xt::col(pts, 2);
  xt::xtensor<double, 1> integrand;
  for (std::size_t k = 0; k < psize; ++k)
  {
    auto pk = xt::col(polyset_at_Qpts, k);
    for (std::size_t d = 0; d < 3; ++d)
    {
      for (std::size_t a = 0; a < (degree > 1 ? 3 : 2); ++a)
      {
        for (int index = 0; index <= degree; ++index)
        {
          auto q_a = xt::col(pts, a);
          integrand = wts * pk;
          if (a == 0)
          {
            if (d == 0)
              integrand *= q1 * q2;
            else if (d == 1)
              integrand *= 0;
            else if (d == 2)
              integrand *= -q0 * q1;

            for (int i = 0; i < index; ++i)
              integrand *= q0;
            for (int i = 0; i < degree - 1 - index; ++i)
              integrand *= q2;
          }
          else if (a == 1)
          {
            if (d == 0)
              integrand *= 0;
            else if (d == 1)
              integrand *= q0 * q2;
            else if (d == 2)
              integrand *= -q0 * q1;

            for (int i = 0; i < index; ++i)
              integrand *= q1;
            for (int i = 0; i < degree - 1 - index; ++i)
              integrand *= q2;
          }
          else if (a == 2)
          {
            if (d == 0)
              integrand *= q1 * q2;
            else if (d == 1)
              integrand *= -q0 * q2;
            else if (d == 2)
              integrand *= 0;

            for (int i = 0; i < index; ++i)
              integrand *= q0;
            for (int i = 0; i < degree - 1 - index; ++i)
              integrand *= q1;
          }

          wcoeffs(3 * nv + 3 * index + a, psize * d + k) = xt::sum(integrand)();
        }
      }
    }
  }

  int c = 3 * nv + (degree > 1 ? 3 : 2) * degree;
  std::vector<std::array<int, 3>> indices;
  for (std::size_t s = 1; s <= 3; ++s)
  {
    indices = serendipity_3d_indices(s + degree + 1, s);
    for (std::array<int, 3> i : indices)
    {
      for (std::size_t k = 0; k < psize; ++k)
      {
        for (int d = 0; d < 3; ++d)
        {
          integrand = wts * xt::col(polyset_at_Qpts, k);
          for (int d2 = 0; d2 < 3; ++d2)
          {
            auto q_d2 = xt::col(pts, d2);
            if (d == d2)
            {
              integrand *= i[d2];
              for (int j = 0; j < i[d2] - 1; ++j)
                integrand *= q_d2;
            }
            else
            {
              for (int j = 0; j < i[d2]; ++j)
                integrand *= q_d2;
            }
          }

          wcoeffs(c, psize * d + k) = xt::sum(integrand)();
        }
      }
      ++c;
    }
  }

  return wcoeffs;
}
//----------------------------------------------------------------------------
} // namespace

//----------------------------------------------------------------------------
FiniteElement basix::element::create_serendipity(
    cell::type celltype, int degree, element::lagrange_variant lvariant,
    element::dpc_variant dvariant, bool discontinuous)
{
  if (degree == 0)
    throw std::runtime_error("Cannot create degree 0 serendipity");

  if (celltype != cell::type::interval and celltype != cell::type::quadrilateral
      and celltype != cell::type::hexahedron)
  {
    throw std::runtime_error("Invalid celltype");
  }

  if (lvariant == element::lagrange_variant::unset)
  {
    if (degree < 3)
      lvariant = element::lagrange_variant::equispaced;
    else
      throw std::runtime_error("serendipity elements of degree > 2 need to be "
                               "given a Lagrange variant.");
  }

  if (dvariant == element::dpc_variant::unset
      and celltype != cell::type::interval)
  {
    if (degree == 4)
      dvariant = element::dpc_variant::simplex_equispaced;
    if (degree > 4)
      throw std::runtime_error(
          "serendipity elements of degree > 4 need to be given a DPC variant.");
  }

  const std::vector<std::vector<std::vector<int>>> topology
      = cell::topology(celltype);
  const std::size_t tdim = cell::topological_dimension(celltype);

  std::array<std::vector<xt::xtensor<double, 3>>, 4> M;
  std::array<std::vector<xt::xtensor<double, 2>>, 4> x;

  // dim 0 (vertices)
  const xt::xtensor<double, 2> geometry = cell::geometry(celltype);
  const std::size_t num_vertices = geometry.shape(0);
  M[0] = std::vector<xt::xtensor<double, 3>>(num_vertices,
                                             xt::ones<double>({1, 1, 1}));
  x[0].resize(geometry.shape(0));
  for (std::size_t i = 0; i < x[0].size(); ++i)
  {
    x[0][i] = xt::reshape_view(
        xt::row(geometry, i), {static_cast<std::size_t>(1), geometry.shape(1)});
  }

  if (degree >= 2)
  {
    FiniteElement moment_space = element::create_lagrange(
        cell::type::interval, degree - 2, lvariant, true);
    std::tie(x[1], M[1]) = moments::make_integral_moments(
        moment_space, celltype, 1, 2 * degree - 2);
  }
  else
  {
    x[1] = std::vector<xt::xtensor<double, 2>>(
        topology[1].size(), xt::xtensor<double, 2>({0, tdim}));
    M[1] = std::vector<xt::xtensor<double, 3>>(
        topology[1].size(), xt::xtensor<double, 3>({0, 1, 0}));
  }

  if (tdim >= 2)
  {
    if (degree >= 4)
    {
      FiniteElement moment_space = element::create_dpc(
          cell::type::quadrilateral, degree - 4, dvariant, true);
      std::tie(x[2], M[2]) = moments::make_integral_moments(
          moment_space, celltype, 1, 2 * degree - 4);
    }
    else
    {
      x[2] = std::vector<xt::xtensor<double, 2>>(
          topology[2].size(), xt::xtensor<double, 2>({0, tdim}));
      M[2] = std::vector<xt::xtensor<double, 3>>(
          topology[2].size(), xt::xtensor<double, 3>({0, 1, 0}));
    }
  }

  if (tdim == 3)
  {
    if (degree >= 6)
    {
      std::tie(x[3], M[3]) = moments::make_integral_moments(
          element::create_dpc(cell::type::hexahedron, degree - 6, dvariant,
                              true),
          celltype, 1, 2 * degree - 6);
    }
    else
    {
      x[3] = std::vector<xt::xtensor<double, 2>>(
          topology[3].size(), xt::xtensor<double, 2>({0, tdim}));
      M[3] = std::vector<xt::xtensor<double, 3>>(
          topology[3].size(), xt::xtensor<double, 3>({0, 1, 0}));
    }
  }

  xt::xtensor<double, 2> wcoeffs;
  if (tdim == 1)
    wcoeffs = xt::eye<double>(degree + 1);
  else if (tdim == 2)
    wcoeffs = make_serendipity_space_2d(degree);
  else if (tdim == 3)
    wcoeffs = make_serendipity_space_3d(degree);


  if (discontinuous)
  {
    std::tie(x, M) = element::make_discontinuous(x, M, tdim, 1);
  }

  return FiniteElement(element::family::serendipity, celltype, degree, {1},
                       wcoeffs, x, M, maps::type::identity, discontinuous,
                       degree < static_cast<int>(tdim) ? 1 : degree / tdim, {},
                       lvariant, dvariant);
}
//----------------------------------------------------------------------------
FiniteElement basix::element::create_serendipity_div(cell::type celltype,
                                                     int degree,
                                                     bool discontinuous)
{
  if (degree == 0)
    throw std::runtime_error("Cannot create degree 0 serendipity");

  if (celltype != cell::type::quadrilateral and celltype != cell::type::hexahedron)
  {
    throw std::runtime_error("Invalid celltype");
  }

  const std::vector<std::vector<std::vector<int>>> topology
      = cell::topology(celltype);
  const std::size_t tdim = cell::topological_dimension(celltype);
  const cell::type facettype
      = (tdim == 2) ? cell::type::interval : cell::type::quadrilateral;

  std::array<std::vector<xt::xtensor<double, 3>>, 4> M;
  std::array<std::vector<xt::xtensor<double, 2>>, 4> x;

  for (std::size_t i = 0; i < tdim - 1; ++i)
  {
    x[i] = std::vector<xt::xtensor<double, 2>>(
        cell::num_sub_entities(celltype, i), xt::xtensor<double, 2>({0, tdim}));
    M[i] = std::vector<xt::xtensor<double, 3>>(
        cell::num_sub_entities(celltype, i),
        xt::xtensor<double, 3>({0, tdim, 0}));
  }

  // TODO: Lagrange variant here
  // TODO: DPC variant
  FiniteElement facet_moment_space
      = facettype == cell::type::interval
            ? element::create_lagrange(
                facettype, degree, element::lagrange_variant::legendre, true)
            : element::create_dpc(facettype, degree,
                                  element::dpc_variant::legendre, true);
  std::tie(x[tdim - 1], M[tdim - 1]) = moments::make_normal_integral_moments(
      facet_moment_space, celltype, tdim, 2 * degree);

  if (degree >= 2)
  {
    // TODO: DPC variant
    FiniteElement cell_moment_space = element::create_dpc(
        celltype, degree - 2, element::dpc_variant::legendre, true);
    std::tie(x[tdim], M[tdim]) = moments::make_integral_moments(
        cell_moment_space, celltype, tdim, 2 * degree - 2);
  }
  else
  {
    x[tdim] = std::vector<xt::xtensor<double, 2>>(
        cell::num_sub_entities(celltype, tdim),
        xt::xtensor<double, 2>({0, tdim}));
    M[tdim] = std::vector<xt::xtensor<double, 3>>(
        cell::num_sub_entities(celltype, tdim),
        xt::xtensor<double, 3>({0, tdim, 0}));
  }

  xt::xtensor<double, 2> wcoeffs;
  if (tdim == 2)
    wcoeffs = make_serendipity_div_space_2d(degree);
  else if (tdim == 3)
    wcoeffs = make_serendipity_div_space_3d(degree);


  if (discontinuous)
  {
    std::tie(x, M) = element::make_discontinuous(x, M, tdim, tdim);
  }

  return FiniteElement(element::family::BDM, celltype, degree + 1, {tdim},
                       wcoeffs, x, M, maps::type::contravariantPiola,
                       discontinuous, degree / tdim);
}
//-----------------------------------------------------------------------------
FiniteElement basix::element::create_serendipity_curl(cell::type celltype,
                                                      int degree,
                                                      bool discontinuous)
{
  if (degree == 0)
    throw std::runtime_error("Cannot create degree 0 serendipity");

  if (celltype != cell::type::quadrilateral and celltype != cell::type::hexahedron)
  {
    throw std::runtime_error("Invalid celltype");
  }

  const std::size_t tdim = cell::topological_dimension(celltype);

  // Evaluate the expansion polynomials at the quadrature points
  auto [Qpts, _wts] = quadrature::make_quadrature(quadrature::type::Default,
                                                  celltype, 2 * degree);
  auto wts = xt::adapt(_wts);
  xt::xtensor<double, 2> polyset_at_Qpts = xt::view(
      polyset::tabulate(celltype, degree, 0, Qpts), 0, xt::all(), xt::all());

  xt::xtensor<double, 2> wcoeffs;
  if (tdim == 2)
    wcoeffs = make_serendipity_curl_space_2d(degree);
  else if (tdim == 3)
    wcoeffs = make_serendipity_curl_space_3d(degree);

  std::array<std::vector<xt::xtensor<double, 3>>, 4> M;
  std::array<std::vector<xt::xtensor<double, 2>>, 4> x;

  x[0] = std::vector<xt::xtensor<double, 2>>(
      cell::num_sub_entities(celltype, 0), xt::xtensor<double, 2>({0, tdim}));
  M[0] = std::vector<xt::xtensor<double, 3>>(
      cell::num_sub_entities(celltype, 0),
      xt::xtensor<double, 3>({0, tdim, 0}));

  // TODO: Lagrange variants
  FiniteElement edge_moment_space = element::create_lagrange(
      cell::type::interval, degree, element::lagrange_variant::legendre, true);

  std::tie(x[1], M[1]) = moments::make_tangent_integral_moments(
      edge_moment_space, celltype, tdim, 2 * degree);

  if (degree >= 2)
  {
    // Face integral moment
    // TODO: DPC variant
    FiniteElement moment_space
        = element::create_dpc(cell::type::quadrilateral, degree - 2,
                              element::dpc_variant::legendre, true);
    std::tie(x[2], M[2]) = moments::make_integral_moments(
        moment_space, celltype, tdim, 2 * degree - 2);
  }
  else
  {
    x[2] = std::vector<xt::xtensor<double, 2>>(
        cell::num_sub_entities(celltype, 2), xt::xtensor<double, 2>({0, tdim}));
    M[2] = std::vector<xt::xtensor<double, 3>>(
        cell::num_sub_entities(celltype, 2),
        xt::xtensor<double, 3>({0, tdim, 0}));
  }

  if (tdim == 3)
  {
    if (degree >= 4)
    {
      // Interior integral moment
      // TODO: DPC variant
      std::tie(x[3], M[3]) = moments::make_integral_moments(
          element::create_dpc(cell::type::hexahedron, degree - 4,
                              element::dpc_variant::legendre, true),
          celltype, tdim, 2 * degree - 4);
    }
    else
    {
      x[3] = std::vector<xt::xtensor<double, 2>>(
          cell::num_sub_entities(celltype, 3),
          xt::xtensor<double, 2>({0, tdim}));
      M[3] = std::vector<xt::xtensor<double, 3>>(
          cell::num_sub_entities(celltype, 3),
          xt::xtensor<double, 3>({0, tdim, 0}));
    }
  }

  const std::vector<std::vector<std::vector<int>>> topology
      = cell::topology(celltype);

  if (discontinuous)
  {
    std::tie(x, M) = element::make_discontinuous(x, M, tdim, tdim);
  }

  return FiniteElement(element::family::N2E, celltype, degree + 1, {tdim},
                       wcoeffs, x, M, maps::type::covariantPiola, discontinuous,
                       (degree == 2 && tdim == 3) ? 1 : degree / tdim);
}
//-----------------------------------------------------------------------------
