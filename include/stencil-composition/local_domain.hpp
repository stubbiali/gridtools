#pragma once

#include <boost/mpl/range_c.hpp>
#include <boost/mpl/fold.hpp>
#include <boost/mpl/sort.hpp>
#include <boost/mpl/push_back.hpp>
#include <boost/mpl/size.hpp>
#include <boost/mpl/set.hpp>
#include <boost/fusion/view/zip_view.hpp>
#include <boost/fusion/include/for_each.hpp>
#include <boost/utility.hpp>
#include "../common/host_device.hpp"
#include "../common/gpu_clone.hpp"
#include "../common/is_temporary_storage.hpp"
#include "../common/generic_metafunctions/is_sequence_of.hpp"
#include "arg.hpp"
#include "../common/generic_metafunctions/histogram.hpp"
#include "../common/generic_metafunctions/scan.hpp"
#include "../common/generic_metafunctions/lazy_range.hpp"
#include "../common/generic_metafunctions/expand_vector.hpp"
#include "../gt_for_each/for_each.hpp"

#include <boost/fusion/include/as_set.hpp>

namespace gridtools {

    namespace local_domain_aux {
        template <typename List, typename Index>
        struct get_index {
            typedef typename boost::mpl::at<
                List,
                Index
                >::type type;
        };

        struct get_storage {
            template <typename U>
            struct apply {
                typedef typename U::storage_type* type;
            };
        };

        template <typename ArgList>
        struct assign_storage_pointers {

            ArgList const& m_arg_list;

            GT_FUNCTION_WARNING
            assign_storage_pointers(ArgList const& arg_list_)
                : m_arg_list(arg_list_)
            {}

            template <typename ZipElem>
            GT_FUNCTION_WARNING
            void operator()(ZipElem const& ze) const {
                typedef typename boost::remove_reference<typename boost::fusion::result_of::at_c<ZipElem, 0>::type>::type::index_type index;

                boost::fusion::at_c<1>(ze) =
#ifdef __CUDACC__ // ugly ifdef. TODO: way to remove it?
                    boost::fusion::at<index>(m_arg_list)->gpu_object_ptr;
#else
                    boost::fusion::at<index>(m_arg_list);
#endif
            }
        };


        template <typename LocalMetaData, typename ActualMetaData>
        struct assign_fusion_maps {

            ActualMetaData const& m_actual;

            GT_FUNCTION_WARNING
            assign_fusion_maps(ActualMetaData const& actual_)
                :
                m_actual(actual_)
            {}

            template <typename Local>
            GT_FUNCTION_WARNING
            void operator()(Local& local_) const {
                // typedef typename boost::fusion::result_of::at_c<ZipElem, 0>::type index;
                local_ =
#ifdef __CUDACC__ // ugly ifdef. TODO: way to remove it?
                    boost::fusion::at_key<Local>(m_actual)->gpu_object_ptr;
#else
                    boost::fusion::at_key<Local>(m_actual);
#endif
            }
        };

        /** Just extract the storage types. In case of temporaries, these types
            are the storage types containing the storage classes that contains
            the "repositories" of all the perthread containers.
        */
        template <typename StorageList>
        struct extract_types {
            template <typename ElemType>
            struct apply {
                typedef typename boost::remove_reference<
                    typename boost::fusion::result_of::at<StorageList, typename ElemType::index_type>::type
                                                         >::type type;
            };
        };

        /** Just extract the storage types. In case of temporaries, these types
            are the storage types containing the actual storage types used by the
            individual threads. This requires a difference w.r.t. extract_types
            for how to deal with temporaries.

            Since certain modifications happend this metafunction is actually
            identical, in behavior, with extract_types.
        */
        template <typename StorageList>
        struct extract_actual_types {

            template <typename Storage, typename Enable=void>
            struct check_if_temporary;

            template <typename Storage>
            struct check_if_temporary<Storage, typename boost::enable_if_c<is_temporary_storage<Storage>::value>::type> {
                typedef Storage type;
            };

            template <typename Storage>
            struct check_if_temporary<Storage, typename boost::disable_if_c<is_temporary_storage<Storage>::value>::type> {
                typedef Storage type;
            };


            template <typename ElemType>
            struct apply {
                typedef typename check_if_temporary<
                    typename boost::remove_reference<
                        typename boost::fusion::result_of::at<StorageList, typename ElemType::index_type>::type
                        >::type
                    >::type type;
            };
        };

        template<typename Storage>
        struct extract_meta_data{
            typedef typename Storage::meta_data_t// ::value_t
            type;
        };

    } // namespace gt_aux



    /**
     * This is the base class for local_domains to extract the proper iterators/storages from the full domain
     * to adapt it for a particular functor. There is one version which provide coordinates to the functor
     * and one that does not
     *
     * @tparam EsfDescriptor The descriptor of the elementary stencil function
     * @tparam Domain The full domain type
     */
    template<typename T>
    struct local_domain_base;

    template<typename S,typename M,typename E, bool I> class local_domain;

    template <typename StoragePointers, typename MetaStoragePointers, typename EsfArgs, bool IsStateful>
    struct local_domain_base<local_domain<StoragePointers, MetaStoragePointers, EsfArgs, IsStateful> >
        : public clonable_to_gpu<local_domain<StoragePointers, MetaStoragePointers, EsfArgs, IsStateful> >
    {
        template <typename I>
        struct extract_index{
            typedef typename I::index_type type;
        };

        struct extract_index_lambda{
            template <typename I>
            struct apply{
                typedef typename extract_index<I>::type type;
            };
        };



        typedef local_domain<StoragePointers, MetaStoragePointers, EsfArgs, IsStateful> derived_t;

        typedef local_domain_base<derived_t> this_type;

        typedef EsfArgs esf_args;

        typedef StoragePointers storage_pointers_t;

        typedef MetaStoragePointers meta_storage_pointers_t;

        typedef boost::mpl::range_c<uint_t, 0, boost::mpl::size<esf_args>::type::value > the_range;

        //! creates a vector of placeholders associated with a linear range
        typedef typename boost::mpl::fold<the_range,
                                          boost::mpl::vector<>,
                                          boost::mpl::push_back<
                                              boost::mpl::_1,
                                              local_domain_aux::get_index<esf_args,  boost::mpl::_2>
                                              >
                                          >::type domain_indices_t;

        /** extracts the static_int indices from the args */
        typedef typename boost::mpl::transform<domain_indices_t,
                                               extract_index_lambda
                                               >::type domain_indices_range_t;


        /** creates a vector of storage types from the StoragePointers sequence */
        typedef typename boost::mpl::fold<domain_indices_t,
                                          boost::mpl::vector<>,
                                          boost::mpl::push_back<
                                              boost::mpl::_1,
                                              typename local_domain_aux::
                                              extract_types<
                                                  StoragePointers>::template apply<boost::mpl::_2>
                                              >
                                          >::type mpl_storages;

        /** creates a vector of storage types from the StoragePointers sequence */
        typedef typename boost::mpl::fold<domain_indices_t,
                                          boost::mpl::vector<>,
                                          boost::mpl::push_back<
                                              boost::mpl::_1,
                                              typename local_domain_aux::extract_actual_types<
                                                  StoragePointers>::template apply<boost::mpl::_2>
                                              >
                                          >::type mpl_actual_storages;


        /** creates a vector of storage types from the StoragePointers sequence */
        typedef typename boost::mpl::fold<mpl_storages,
                                          boost::mpl::vector0<>,
                                          boost::mpl::push_back<
                                              boost::mpl::_1,
                                              typename local_domain_aux::extract_meta_data<
                                                  boost::remove_pointer<
                                                  boost::mpl::_2 >
                                                  >
                                              >
                                          >::type::type mpl_meta_data_t;


        typedef typename boost::mpl::fold<boost::mpl::range_c<uint_t, 0, boost::mpl::size<mpl_meta_data_t>::value>
                                          , boost::mpl::map0<>
                                          , boost::mpl::insert
                                          <boost::mpl::_1, boost::mpl::pair
                                           <boost::mpl::at
                                            <mpl_meta_data_t, boost::mpl::_2>,
                                            boost::mpl::_2
                                            >
                                           >
                                          >::type storage_metadata_map;


        // //! ordering map in the storage according to the metadata indices
        // typedef typename boost::mpl::sort
        // < typename lazy_range<static_int<0>, typename boost::mpl::minus<typename boost::mpl::size<mpl_meta_data_t >::type, static_int<1> >::type >::type
        //   , boost::mpl::less<extract_index<boost::mpl::at<mpl_meta_data_t, boost::mpl::_1 > >
        //                      , extract_index<boost::mpl::at<mpl_meta_data_t,boost::mpl::_2> >
        //                     >
        //  >
        // ::type sort_t;







        // //! get the sorted metadata indices in a vector
        // typedef typename boost::mpl::fold<sort_t,
        //                                   boost::mpl::vector0<>,
        //                                   boost::mpl::push_back<
        //                                       boost::mpl::_1,
        //                                       extract_index< boost::mpl::at<mpl_meta_data_t, boost::mpl::_2> >
        //                                       >
        //                                   >::type sorted_metadata_indices_t;

        // //! compute the histogram of the vector
        // typedef typename histogram<sorted_metadata_indices_t>::type mpl_storage_multiplicity;

        // //! compute the scan of the histogram (useless)
        // typedef typename exclusive_scan< mpl_storage_multiplicity >::type mpl_multiplicity_scan;

        // //! create a linear range as an mpl::vector (not a range_c)
        // typedef typename lazy_range
        // <static_int<0>, typename boost::mpl::minus
        //  <typename boost::mpl::size
        //   <mpl_storage_multiplicity>::type, static_int<1> >::type >::type metadata_indices_range_t;

        // //! map the index vector to the original dimension of the storage list
        // typedef typename expand< metadata_indices_range_t
        //                          // mpl_multiplicity_scan
        //                          , mpl_storage_multiplicity >::type mpl_multiplicity_map;



        // typedef// typename boost::fusion::result_of::as_vector<
        //     typename boost::mpl::transform<sort_t, // typename boost::add_pointer<
        //                                                         boost::mpl::_1// >
        //                                    >::type
        //     // >::type
        // local_metadata_mpl_t;
        typedef mpl_meta_data_t local_metadata_mpl_t;







        typedef typename boost::fusion::result_of::as_vector<mpl_storages>::type local_args_type;
        typedef typename boost::fusion::result_of::as_vector<mpl_actual_storages>::type actual_args_type;


        // typedef typename boost::fusion::result_of::as_set<
        //     typename boost::mpl::transform<local_metadata_mpl_t, pointer<
        //                                                         boost::add_const< boost::mpl::_1> > >::type
        //     >::type local_metadata_type;

        typedef typename boost::fusion::result_of::as_vector<
            typename boost::mpl::transform<local_metadata_mpl_t, pointer<
                                                                boost::add_const< boost::mpl::_1> > >::type
            >::type local_metadata_type;

        local_args_type m_local_args;
        local_metadata_type m_local_metadata;

        template <typename Dom, typename IsActuallyClonable, uint_t DUMMY = 0>
        struct pointer_if_clonable {
            static Dom* get(Dom* d) {
                return d;
            }
        };

        template <typename Dom, uint_t DUMMY>
        struct pointer_if_clonable<Dom, boost::true_type, DUMMY> {
            static Dom* get(Dom* d) {
                return d->gpu_object_ptr;
            }
        };

        GT_FUNCTION_WARNING
        local_domain_base() {}

        template <typename ActualArgs, typename ActualMetaData>
        GT_FUNCTION
        void init(ActualArgs const& actual_args_, ActualMetaData const& actual_metadata_)
        {
            typedef boost::fusion::vector<domain_indices_t const&, local_args_type&> to_zip_t;
            typedef boost::fusion::zip_view<to_zip_t> zipping_t;

            to_zip_t z(domain_indices_t(), m_local_args);

            boost::fusion::for_each(zipping_t(z), local_domain_aux::assign_storage_pointers<ActualArgs>(actual_args_));

            // typedef boost::fusion::vector<metadata_indices_range_t const&, local_metadata_type&> to_zip2_t;
            // typedef boost::fusion::zip_view<to_zip2_t> zipping2_t;

            // to_zip2_t z2(metadata_indices_range_t(), m_local_metadata);
            // boost::fusion::for_each(zipping2_t(z2), local_domain_aux::assign_meta_storages<ActualMetaData>(actual_metadata));

            // for_each<domain_indices_range_t>(local_domain_aux::assign_fusion_vectors<local_args_type, ActualArgs>(m_local_args, actual_args_));

            //metadata_indices_range_t
            boost::fusion::for_each(m_local_metadata, local_domain_aux::assign_fusion_maps<local_metadata_type, ActualMetaData>(actual_metadata_));
        }

        __device__
        local_domain_base(local_domain_base const& other)
            : m_local_args(other.m_local_args)
            , m_local_metadata(other.m_local_metadata)
            { }

        template <typename T>
        void info(T const&) const {
            T::info();
            std::cout << "[" << boost::mpl::at_c<esf_args, T::index_type::value>::type::index_type::value << "] ";
        }

        struct show_local_args_info {
            template <typename T>
            void operator()(T const & e) const {
                e->info();
            }
        };

        GT_FUNCTION
        void info() const {
            std::cout << "        -----v SHOWING LOCAL ARGS BELOW HERE v-----" << std::endl;
            boost::fusion::for_each(m_local_args, show_local_args_info());
            std::cout << "        -----^ SHOWING LOCAL ARGS ABOVE HERE ^-----" << std::endl;
        }
    };

    template <typename T>
    struct is_meta_storage_wrapper;
    /**
     * This class extract the proper iterators/storages from the full domain
     * to adapt it for a particular functor. This version does not provide coordinates
     * to the function operator
     *
     * @tparam EsfDescriptor The descriptor of the elementary stencil function
     * @tparam Domain The full domain type
     */
    template <typename StoragePointers, typename MetaData, typename EsfArgs, bool IsStateful>
    struct local_domain : public local_domain_base< local_domain<StoragePointers, MetaData, EsfArgs, IsStateful> > {

        // GRIDTOOLS_STATIC_ASSERT((is_sequence_of<MetaData, is_meta_storage_wrapper>::value),"Local domain contains wront type for parameter meta storages");
        GRIDTOOLS_STATIC_ASSERT((is_sequence_of<EsfArgs, is_arg>::value),"Local domain contains wront type for parameter placeholders");
        typedef local_domain_base<local_domain<StoragePointers,MetaData,EsfArgs,IsStateful> > base_type;
        typedef StoragePointers storage_pointers;
        typedef EsfArgs esf_args;

        GT_FUNCTION
        local_domain() {}

        __device__
        local_domain(local_domain const& other)
            : base_type(other)
        {}

        template <typename ArgList, typename MetaDataList>
        GT_FUNCTION
        void init(ArgList const& arg_list, MetaDataList const& meta_data_, uint_t, uint_t, uint_t)
        {
            base_type::init(arg_list, meta_data_);
#ifdef __VERBOSE__
#ifndef __CUDACC__
            std::cout << "LOCAL DOMAIN" << std::endl;
#endif
#endif
        }

/**stub methods*/
        GT_FUNCTION
        uint_t i() const {return 1e9; }
        GT_FUNCTION
        uint_t j() const {return 1e9; }
        GT_FUNCTION
        uint_t k() const {return 1e9; }
    };

    template <typename StoragePointers, typename MetaData, typename EsfArgs, bool IsStateful>
    std::ostream& operator<<(std::ostream& s, local_domain<StoragePointers,MetaData, EsfArgs, IsStateful> const&) {
        return s << "local_domain<stuff>";
    }

    template<typename T> struct is_local_domain : boost::mpl::false_{};

    template <typename StoragePointers, typename MetaData, typename EsfArgs, bool IsStateful>
    struct is_local_domain<local_domain<StoragePointers, MetaData, EsfArgs, IsStateful> > : boost::mpl::true_{};

    template<typename T> struct local_domain_is_stateful;

    template <typename StoragePointers, typename MetaData, typename EsfArgs, bool IsStateful>
    struct local_domain_is_stateful<local_domain<StoragePointers, MetaData, EsfArgs, IsStateful> > : boost::mpl::bool_<IsStateful>{};

    template<typename T>
    struct local_domain_esf_args;

    template <typename StoragePointers, typename MetaData, typename EsfArgs, bool IsStateful>
    struct local_domain_esf_args<local_domain<StoragePointers, MetaData, EsfArgs, IsStateful> >
    {
        typedef EsfArgs type;
    };

    template<typename T>
    struct local_domain_storage_pointers;

    template <typename StoragePointers, typename MetaData, typename EsfArgs, bool IsStateful>
    struct local_domain_storage_pointers<local_domain<StoragePointers, MetaData, EsfArgs, IsStateful> >
    {
        typedef StoragePointers type;
    };

    template <typename LocalDomain>
    struct meta_storage_pointers
    {
        typedef typename LocalDomain::meta_storage_pointers_t type;
    };

} // namespace gridtools
