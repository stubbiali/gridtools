#pragma once

#include <gridtools.hpp>

#include <stencil_composition/stencil_composition.hpp>

#include <stencil_composition/interval.hpp>
#include <stencil_composition/make_computation.hpp>
#include <tools/verifier.hpp>

#ifdef USE_PAPI_WRAP
#include <papi_wrap.hpp>
#include <papi.hpp>
#endif

/*
  @file This file shows an implementation of the Thomas algorithm, done using stencil operations.

  Important convention: the linear system as usual is represented with 4 vectors: the main diagonal
  (diag), the upper and lower first diagonals (sup and inf respectively), and the right hand side
  (rhs). Note that the dimensions and the memory layout are, for an NxN system
  rank(diag)=N       [xxxxxxxxxxxxxxxxxxxxxxxx]
  rank(inf)=N-1      [0xxxxxxxxxxxxxxxxxxxxxxx]
  rank(sup)=N-1      [xxxxxxxxxxxxxxxxxxxxxxx0]
  rank(rhs)=N        [xxxxxxxxxxxxxxxxxxxxxxxx]
  where x denotes any number and 0 denotes the padding, a dummy value which is not used in
  the algorithm. This choice coresponds to having the same vector index for each row of the matrix.
 */

using gridtools::level;
using gridtools::accessor;
using gridtools::extent;
using gridtools::arg;

namespace tridiagonal {

    using namespace gridtools;
    using namespace enumtype;

#ifdef CXX11_ENABLED
    using namespace expressions;
#endif

    // This is the definition of the special regions in the "vertical" direction
    typedef gridtools::interval< level< 0, 1 >, level< 1, -2 > > x_internal;
    typedef gridtools::interval< level< 0, -1 >, level< 0, -1 > > x_first;
    typedef gridtools::interval< level< 1, -1 >, level< 1, -1 > > x_last;
    typedef gridtools::interval< level< 0, -1 >, level< 1, 1 > > axis;

    struct forward_thomas {
        // four vectors: output, and the 3 diagonals
        typedef accessor< 0, enumtype::inout > out;
        typedef accessor< 1 > inf;                  // a
        typedef accessor< 2 > diag;                 // b
        typedef accessor< 3, enumtype::inout > sup; // c
        typedef accessor< 4, enumtype::inout > rhs; // d
        typedef boost::mpl::vector< out, inf, diag, sup, rhs > arg_list;

        template < typename Domain >
        GT_FUNCTION static void shared_kernel(Domain const &dom) {
#if (defined(CXX11_ENABLED))
            dom(sup{}) = dom(sup{} / (diag{} - sup{z{-1}} * inf{}));
            dom(rhs{}) = dom((rhs{} - inf{} * rhs{z(-1)}) / (diag{} - sup{z(-1)} * inf{}));
#else
            dom(sup()) = dom(sup()) / (dom(diag()) - dom(sup(z(-1))) * dom(inf()));
            dom(rhs()) = (dom(rhs()) - dom(inf()) * dom(rhs(z(-1)))) / (dom(diag()) - dom(sup(z(-1))) * dom(inf()));
#endif
        }

        template < typename Domain >
        GT_FUNCTION static void Do(Domain const &dom, x_internal) {
            shared_kernel(dom);
        }

        template < typename Domain >
        GT_FUNCTION static void Do(Domain const &dom, x_last) {
            shared_kernel(dom);
        }

        template < typename Domain >
        GT_FUNCTION static void Do(Domain const &dom, x_first) {
            dom(sup()) = dom(sup()) / dom(diag());
            dom(rhs()) = dom(rhs()) / dom(diag());
        }
    };

    struct backward_thomas {
        typedef accessor< 0, enumtype::inout > out;
        typedef accessor< 1 > inf;                  // a
        typedef accessor< 2 > diag;                 // b
        typedef accessor< 3, enumtype::inout > sup; // c
        typedef accessor< 4, enumtype::inout > rhs; // d
        typedef boost::mpl::vector< out, inf, diag, sup, rhs > arg_list;

        template < typename Domain >
        GT_FUNCTION static void shared_kernel(Domain &dom) {
#if (defined(CXX11_ENABLED))
            dom(out()) = dom(rhs{} - sup{} * out{0, 0, 1});
#else
            dom(out()) = dom(rhs()) - dom(sup()) * dom(out(0, 0, 1));
#endif
        }

        template < typename Domain >
        GT_FUNCTION static void Do(Domain const &dom, x_internal) {
            shared_kernel(dom);
        }

        template < typename Domain >
        GT_FUNCTION static void Do(Domain const &dom, x_first) {
            shared_kernel(dom);
        }

        template < typename Domain >
        GT_FUNCTION static void Do(Domain const &dom, x_last) {
            dom(out()) = dom(rhs());
        }
    };

    std::ostream &operator<<(std::ostream &s, backward_thomas const) { return s << "backward_thomas"; }
    std::ostream &operator<<(std::ostream &s, forward_thomas const) { return s << "forward_thomas"; }

    bool test(uint_t d1, uint_t d2, uint_t d3) {

        if (d3 != 6)
            std::cout << "WARNING: This test is only working with 6 k levels,"
                         "to guarantee that result can be validated to 1"
                      << std::endl;
        d3 = 6;

#ifdef USE_PAPI_WRAP
        int collector_init = pw_new_collector("Init");
        int collector_execute = pw_new_collector("Execute");
#endif

#ifdef CUDA_EXAMPLE
#define BACKEND backend< Cuda, GRIDBACKEND, Block >
#else
#ifdef BACKEND_BLOCK
#define BACKEND backend< Host, GRIDBACKEND, Block >
#else
#define BACKEND backend< Host, GRIDBACKEND, Naive >
#endif
#endif

        //    typedef gridtools::STORAGE<double, gridtools::layout_map<0,1,2> > storage_type;
        typedef gridtools::layout_map< 0, 1, 2 > layout_t;
        typedef gridtools::BACKEND::storage_info< 0, layout_t > meta_t;
        typedef gridtools::BACKEND::storage_type< float_type, meta_t >::type storage_type;
        typedef gridtools::BACKEND::temporary_storage_type< float_type, meta_t >::type tmp_storage_type;

        // Definition of the actual data fields that are used for input/output
        // storage_type in(d1,d2,d3,-1, "in"));
        meta_t meta_(d1, d2, d3);
        storage_type out(meta_, 0., "out");
        storage_type inf(meta_, -1., "inf");
        storage_type diag(meta_, 3., "diag");
        storage_type sup(meta_, 1., "sup");
        storage_type rhs(meta_, 3., "rhs");

        storage_type solution(meta_, 1., "sol");

        for (int_t i = 0; i < d1; ++i) {
            for (int_t j = 0; j < d2; ++j) {
                rhs(i, j, 0) = 4.;
                rhs(i, j, 5) = 2.;
            }
        }
        // result is 1

        // Definition of placeholders. The order of them reflect the order the user will deal with them
        // especially the non-temporary ones, in the construction of the domain
        typedef arg< 0, storage_type > p_inf;  // a
        typedef arg< 1, storage_type > p_diag; // b
        typedef arg< 2, storage_type > p_sup;  // c
        typedef arg< 3, storage_type > p_rhs;  // d
        typedef arg< 4, storage_type > p_out;

        // An array of placeholders to be passed to the domain
        // I'm using mpl::vector, but the final API should look slightly simpler
        typedef boost::mpl::vector< p_inf, p_diag, p_sup, p_rhs, p_out > accessor_list;

        // construction of the domain. The domain is the physical domain of the problem, with all the physical fields
        // that are used, temporary and not
        // It must be noted that the only fields to be passed to the constructor are the non-temporary.
        // The order in which they have to be passed is the order in which they appear scanning the placeholders in
        // order. (I don't particularly like this)
        gridtools::aggregator_type< accessor_list > domain(boost::fusion::make_vector(&inf, &diag, &sup, &rhs, &out));

        // Definition of the physical dimensions of the problem.
        // The constructor takes the horizontal plane dimensions,
        // while the vertical ones are set according the the axis property soon after
        // gridtools::grid<axis> grid(2,d1-2,2,d2-2);
        uint_t di[5] = {0, 0, 0, d1 - 1, d1};
        uint_t dj[5] = {0, 0, 0, d2 - 1, d2};

        gridtools::grid< axis > grid(di, dj);
        grid.value_list[0] = 0;
        grid.value_list[1] = d3 - 1;

/*
  Here we do lot of stuff
  1) We pass to the intermediate representation ::run function the description
  of the stencil, which is a multi-stage stencil (mss)
  The mss includes (in order of execution) a laplacian, two fluxes which are independent
  and a final step that is the out_function
  2) The logical physical domain with the fields to use
  3) The actual domain dimensions
 */

#ifdef CXX11_ENABLED
        auto
#else
#ifdef __CUDACC__
        gridtools::stencil *
#else
        boost::shared_ptr< gridtools::stencil >
#endif
#endif
            solver = gridtools::make_computation< gridtools::BACKEND >(
                domain,
                grid,
                gridtools::make_multistage // mss_descriptor
                (execute< forward >(),
                    gridtools::make_stage< forward_thomas >(
                        p_out(), p_inf(), p_diag(), p_sup(), p_rhs()) // esf_descriptor
                    ),
                gridtools::make_multistage // mss_descriptor
                (execute< backward >(),
                    gridtools::make_stage< backward_thomas >(
                        p_out(), p_inf(), p_diag(), p_sup(), p_rhs()) // esf_descriptor
                    ));

        solver->ready();
        solver->steady();

        solver->run();

        solver->finalize();

#ifdef BENCHMARK
        std::cout << solver->print_meter() << std::endl;
#endif

#ifdef CXX11_ENABLED
#if FLOAT_PRECISION == 4
        verifier verif(1e-6);
#else
        verifier verif(1e-12);
#endif
        array< array< uint_t, 2 >, 3 > halos{{{0, 0}, {0, 0}, {0, 0}}};
        bool result = verif.verify(grid, solution, out, halos);
#else
#if FLOAT_PRECISION == 4
        verifier verif(1e-6, 0);
#else
        verifier verif(1e-12, 0);
#endif
        bool result = verif.verify(grid, solution, out);
#endif

        return result;
    }
} // namespace tridiagonal
