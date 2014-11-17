#pragma once
#include <boost/lexical_cast.hpp>

#include "../common/basic_utils.h"
#include "../common/gpu_clone.h"
#include "../common/gt_assert.h"
#include "../common/is_temporary_storage.h"
#include "../stencil-composition/backend_traits_host.h"
#include "../stencil-composition/backend_traits_cuda.h"
#include "hybrid_pointer.h"
#include <iostream>

/**@file
   @brief Implementation of the main storage class, used by all backends, for temporary and non-temporary storage

   We define here an important naming convention. We call:

   - the data fields: are contiguous chunks of memory, accessed by 3 (by default, but not necessarily) indexes.
   These structures are univocally defined by 3 (by default) integers. These are currently 2 strides and the total size of the chunks. Note that (in 3D) the relation between these quantities (\f$stride_1\f$, \f$stride_2\f$ and \f$size\f$) and the dimensions x, y and z can be (depending on the storage layout chosen)
   \f$
   size=x*y*z //
   stride_2=x*y //
   stride_1=x .
   \f$
   The quantities \f$size\f$, \f$stride_2\f$ and \f$stride_1\f$ are arranged respectively in m_strides[0], m_strides[1], m_strides[2].
   - the data snapshot: is a single pointer to one data field. The snapshots are arranged in the storages on a 1D array, regardless of the dimension and snapshot they refer to. The arg_type (or arg_decorator) class is
   responsible of computing the correct offests (relative to the given dimension) and address the storages correctly.
   - the storage: is an instance of any storage class, and can contain one or more fields and dimension. Every dimension consists of one or several snaphsots of the fields
   (e.g. if the time T is the current dimension, 3 snapshots can be the fields at t, t+1, t+2)

   The basic_storage class has a 1-1 relation with the data fields, while the subclasses extend the concept of storage to the structure represented in the ASCII picture below. The extension to multiple dimensions
   or to more general cases could be implemented (probably) fairly easily, since the current interface is arbitrary in the number of dimension. The storage layout instead is limited to 2 dimensions, since
   I cannot think of use cases for which more than two dimensions would be necessary.


   NOTE: the constraint of the data fields accessed by the same storage class are the following:
   - the memory layout is one for all the data fields
   - the index used to access the storage position

\verbatim
############### 2D Storage ################
#                    ___________\         #
#                      time     /         #
#                  | |*|*|*|*|*|*|        #
# space, pressure  | |*|*|*|              #
#    energy,...    v |*|*|*|*|*|      	  #
#					                      #
#                     ^ ^ ^ ^ ^ ^	      #
#                     | | | | | |	      #
#                      snapshots	      #
#				                     	  #
############### 2D Storage ################
\endverbatim

The final storage which is effectly instantiated must be "clonable to the GPU", i.e. it must derive from the clonable_to_gpu struct.
This is achieved by defining a class with multiple inheritance.

NOTE CUDA: It is important when subclassing from a storage object to reimplement the __device__ copy constructor, and possibly the method 'copy_data_to_gpu' which are used when cloning the class to the CUDA device.
*/

namespace gridtools {

    namespace _impl
    {

/**@brief Functor updating the pointers on the device */
        struct update_pointer {

#ifdef __CUDACC__
            template < typename StorageType//typename T, typename U, bool B
                       >
            GT_FUNCTION_WARNING
            void operator()(/*base_storage<enumtype::Cuda,T,U,B
                              >*/StorageType *& s) const {
                if (s) {
                    s->copy_data_to_gpu();
                    s->clone_to_gpu();
                    s = s->gpu_object_ptr;
                }
            }
#else
            template <typename StorageType>
            GT_FUNCTION_WARNING
            void operator()(StorageType* s) const {}
#endif

        };
    }//namespace _impl

    namespace _debug{
#ifndef NDEBUG
        struct print_pointer {
            template <typename StorageType>
            GT_FUNCTION_WARNING
            void operator()(StorageType* s) const {
                printf("CIAOOO TATATA %x\n",  s);
            }

#ifdef __CUDACC__
            template < typename T, typename U, bool B
                       >
            GT_FUNCTION_WARNING
            void operator()(base_storage<enumtype::Cuda,T,U,B
                            > *& s) const {
                printf("CIAO POINTER %X\n", s);
            }
#endif
        };
#endif
    }//namespace _debug


/**
   @biref main class for the basic storage
*/
#define FIELDS_DIMENSION 3

    template < enumtype::backend Backend,
               typename ValueType,
               typename Layout,
               bool IsTemporary = false
               >
    struct base_storage
    {
        typedef Layout layout;
        typedef ValueType value_type;
        typedef value_type* iterator_type;
        typedef value_type const* const_iterator_type;
        typedef backend_from_id <Backend> backend_traits_t;
        typedef typename backend_traits_t::template pointer<value_type>::type pointer_type;
        //typedef in order to stopo the type recursion of the derived classes
        typedef base_storage<Backend, ValueType, Layout, IsTemporary> basic_type;
        typedef base_storage<Backend, ValueType, Layout, IsTemporary> original_storage;
        static const ushort_t n_args = 1;

    public:
        base_storage(uint_t dim1, uint_t dim2, uint_t dim3,
                     value_type init = value_type(), std::string const& s = std::string("default name") ):
            m_data( dim1*dim2*dim3 )
            , is_set( true )
            , m_name(s)
            {
                // printf("layout: %d %d %d \n", layout::get(0), layout::get(1), layout::get(2));
#ifndef COMPILE_TIME_STRIDES
                uint_t dims[]={dim1, dim2, dim3};
                m_strides[0]=( dim1*dim2*dim3 );
                m_strides[1]=( dims[layout::template get<2>()]*dims[layout::template get<1>()]);
                m_strides[2]=( dims[layout::template get<2>()] );
#endif

#ifdef _GT_RANDOM_INPUT
                srand(12345);
#endif
                for (uint_t i = 0; i < size(); ++i)
#ifdef _GT_RANDOM_INPUT
                    m_data[i] = init * rand();
#else
                m_data[i] = init;
#endif
                m_data.update_gpu();
            }

        /**@brief destructor: frees the pointers to the data fields */
        virtual ~base_storage(){m_data.free_it();}

        /**@brief device copy constructor*/
        template<typename T>
        __device__
        base_storage(T const& other)
            :
            m_data(other.m_data)
            , is_set(other.is_set)
            , m_name(other.m_name)
            {
                m_strides[0] = other.size();
                m_strides[1] = other.strides(1);
                m_strides[2] = other.strides(2);
            }

      GT_FUNCTION_WARNING
        void copy_data_to_gpu() const {data().update_gpu();}

        explicit base_storage(): m_data((value_type*)NULL), is_set(false), m_name("default_name"){
        }


        std::string const& name() const {
            return m_name;
        }

        static void text() {
            std::cout << BOOST_CURRENT_FUNCTION << std::endl;
        }

        void h2d_update(){
            m_data.update_gpu();
        }

        void d2h_update(){
            m_data.update_cpu();
        }

        void info() const {
            std::cout << dims_coordwise<0>(m_strides) << "x"
                      << dims_coordwise<1>(m_strides) << "x"
                      << dims_coordwise<2>(m_strides) << ", "
                      << std::endl;
        }

        GT_FUNCTION
        const_iterator_type min_addr() const {
            return &(m_data[0]);
        }


        GT_FUNCTION
        const_iterator_type max_addr() const {
            return &(m_data[/*m_size*/m_strides[0]]);
        }

        GT_FUNCTION
        value_type& operator()(uint_t i, uint_t j, uint_t k) {
            /* std::cout<<"indices= "<<i<<" "<<j<<" "<<k<<std::endl; */
            //backend_traits_t::assertion(_index(i,j,k) >= 0);
            backend_traits_t::assertion(_index(i,j,k) < /* m_size*/ m_strides[0]);
            return m_data[_index(i,j,k)];
        }


        GT_FUNCTION
        value_type const & operator()(uint_t i, uint_t j, uint_t k) const {
            //backend_traits_t::assertion(_index(i,j,k) >= 0);
            backend_traits_t::assertion(_index(i,j,k) < /*m_size*/m_strides[0]);
            return m_data[_index(i,j,k)];
        }

        //note: offset returns a signed int because the layout map indexes are signed short ints
        GT_FUNCTION
        int_t offset(int_t i, int_t j, int_t k) const {
            return  m_strides[1]* layout::template find<0>(i,j,k) +  m_strides[2] * layout::template find<1>(i,j,k) + layout::template find<2>(i,j,k);
        }

        GT_FUNCTION
        uint_t size() const {
            return m_strides[0];
        }

        void print() const {
            print(std::cout);
        }

        /**@brief prints a single value of the data field given the coordinates*/
        void print_value(uint_t i, uint_t j, uint_t k){ printf("value(%d, %d, %d)=%f, at index %d on the data\n", i, j, k, m_data[_index(i, j, k)], _index(i, j, k));}

        static const std::string info_string;


        /**@brief return the stride for a specific coordinate, given the vector of strides*/
        template<uint_t Coordinate>
        GT_FUNCTION
        static constexpr uint_t strides(uint_t const* strides){
            return (layout::template pos_<Coordinate>::value==FIELDS_DIMENSION-1) ? 1 : layout::template find<Coordinate>(&strides[1]);
        }


        /**@brief return the stride for a specific coordinate, given the vector of dimensions*/
        template<uint_t Coordinate>
        GT_FUNCTION
        static constexpr uint_t strides_coordwise(uint_t const* dims){
            return (layout::template pos_<Coordinate>::value==FIELDS_DIMENSION-1) ? 1 : layout::template find<Coordinate>(dims)*dims_stridewise<
                (layout::template get_<Coordinate>::value/*layout::template get<Coordinate>())*/)+1>(dims);
        }

        /**@brief return the stride for a specific stride level (i.e. 0 for stride x*y, 1 for stride x, 2 for stride 1), given the vector of dimensions*/
        template<uint_t StrideOrder>
        GT_FUNCTION
        static constexpr uint_t strides_stridewise(uint_t const* dims){
            return (StrideOrder==FIELDS_DIMENSION-1) ? 1 : dims[StrideOrder]*(strides_stridewise<StrideOrder+1>(dims));
        }

        /**@brief return the dimension for a specific coordinate, given the vector of strides*/
        template<uint_t Coordinate>
        GT_FUNCTION
        static constexpr uint_t dims_coordwise(uint_t const* str) {
            return (layout::template pos_<Coordinate>::value==FIELDS_DIMENSION-1) ? str[FIELDS_DIMENSION-1] : (layout::template find<Coordinate>(str))/(str[(layout::template get<Coordinate>())+1]);
        }


        /**@brief return the dimension size corresponding to a specific stride level (i.e. 0 for stride x*y, 1 for stride x, 2 for stride 1), given the vector of strides*/
        template<uint_t StrideOrder>
        GT_FUNCTION
        static constexpr uint_t dims_stridewise(uint_t const* strides){
            return (StrideOrder==FIELDS_DIMENSION-1) ? strides[FIELDS_DIMENSION-1] : strides[StrideOrder]/strides[StrideOrder+1];
        }

        /**@brief printing a portion of the content of the data field*/
        template <typename Stream>
        void print(Stream & stream) const {
            stream << "(" << m_strides[1] << "x"
                   << m_strides[2] << "x"
                   << 1 << ")"
                   << std::endl;
            stream << "| j" << std::endl;
            stream << "| j" << std::endl;
            stream << "v j" << std::endl;
            stream << "---> k" << std::endl;

            ushort_t MI=12;
            ushort_t MJ=12;
            ushort_t MK=12;
            for (uint_t i = 0; i < dims_coordwise<0>(m_strides); i += std::max(( uint_t)1,dims_coordwise<0>(m_strides)/MI)) {
                for (uint_t j = 0; j < dims_coordwise<1>(m_strides); j += std::max(( uint_t)1,dims_coordwise<1>(m_strides)/MJ)) {
                    for (uint_t k = 0; k < dims_coordwise<2>(m_strides); k += std::max(( uint_t)1,dims_coordwise<1>(m_strides)/MK))
                    {
                        stream << "["/*("
                                       << i << ","
                                       << j << ","
                                       << k << ")"*/
                               << this->operator()(i,j,k) << "] ";
                    }
                    stream << std::endl;
                }
                stream << std::endl;
            }
            stream << std::endl;
        }

        GT_FUNCTION
        uint_t _index(uint_t i, uint_t j, uint_t k) const {
            uint_t index;
            if (IsTemporary) {
                assert(false);
                index =
                    m_strides[1]* (modulus(layout::template find<0>(i,j,k),dims_coordwise<0>(m_strides))) +
                    m_strides[2]*modulus(layout::template find<1>(i,j,k), dims_coordwise<1>(m_strides)) +
                    modulus(layout::template find<2>(i,j,k), dims_coordwise<2>(m_strides));
            } else {
                index = m_strides[1] * layout::template find<0>(i,j,k) +
                    m_strides[2]* layout::template find<1>(i,j,k) +
                    layout::template find<2>(i,j,k);
            }
            return index;
        }

        template <uint_t Coordinate>
        GT_FUNCTION
        void increment(uint_t& /*block*/, uint_t* index){
            *index+=strides<Coordinate>(m_strides);
        }

        template <uint_t Coordinate>
        GT_FUNCTION
        void decrement(uint_t& /*block*/, uint_t* index){
            *index-=strides<Coordinate>(m_strides);
        }

        template <uint_t Coordinate>
        GT_FUNCTION
        void increment(uint_t const& dimension, uint_t& /*block*/, uint_t* index){
            *index+=strides<Coordinate>(m_strides)*dimension;
        }

        template <uint_t Coordinate>
        GT_FUNCTION
        void decrement(uint_t dimension, uint_t& /*block*/, uint_t* index){
            *index-=strides<Coordinate>(m_strides)*dimension;
        }

        GT_FUNCTION
        pointer_type const& data() const {return m_data;}

        GT_FUNCTION
        typename pointer_type::pointee_t* get_address() const {
            return m_data.get();}

        GT_FUNCTION
        pointer_type const* fields() const {return &m_data;}

        template<ushort_t I>
        GT_FUNCTION
        uint_t dims() const {return dims_coordwise<I>(m_strides);}

        GT_FUNCTION
        uint_t strides(ushort_t i) const {
            //"you might thing that you are accessing a stride,\n
            // but you are indeed accessing the whole storage dimension."
            assert(i!=0);
            return m_strides[i];
        }

        pointer_type m_data;

    protected:
        bool is_set;
        const std::string& m_name;
#ifdef COMPILE_TIME_STRIDES
        static const uint_t m_strides[/*3*/FIELDS_DIMENSION]={( dim1*dim2*dim3 ),( dims[layout::template get<2>()]*dims[layout::template get<1>()]),( dims[layout::template get<2>()] )};
#else
        uint_t m_strides[FIELDS_DIMENSION];
#endif
    };


    struct print_index{
        template <typename BaseStorage>
        GT_FUNCTION
        void operator()(BaseStorage* b ) const {printf("index -> %d, address %lld, 0x%08x \n", b->index(), &b->index(), &b->index());}
    };

    template <uint_t Coordinate>
    struct incr{

        GT_FUNCTION
        incr(){};

        template<typename BaseStorage>
        GT_FUNCTION
        void operator()(BaseStorage * b) const {b->template increment<Coordinate>();}
    };

    template <uint_t Coordinate>
    struct incr_stateful{

        GT_FUNCTION
        incr_stateful(uint dimension):m_dimension(dimension){};

        template<typename BaseStorage>
        GT_FUNCTION
        void operator()(BaseStorage * b) const {b->template increment<Coordinate>(m_dimension);}
    private:
        uint_t m_dimension;
    };

    template <uint_t Coordinate>
    struct decr{
        template<typename BaseStorage>
        GT_FUNCTION
        void operator()(BaseStorage * b) const {b->template decrement<Coordinate>();}
    };


/** @brief storage class containing a buffer of data snapshots
    the goal of this struct is to  implement a cash for the solutions, in order to ease the finite differencin between the different fields.
*/

    template < typename Storage, ushort_t ExtraWidth>
    struct extend_width : public Storage//, clonable_to_gpu<extend_width<Storage, ExtraWidth> >
    {
        typedef Storage super;
        typedef typename super::pointer_type pointer_type;

        typedef typename super::original_storage original_storage;
        typedef typename super::iterator_type iterator_type;
        typedef typename super::value_type value_type;
        explicit extend_width(uint_t dim1, uint_t dim2, uint_t dim3,
                              value_type init = value_type(), std::string const& s = std::string("default name") ): super(dim1, dim2, dim3, init, s)/* , m_lru(0) */, m_fields() {
            push_back(super::m_data);//first solution is the initialization by default
        }

        __device__
        extend_width(extend_width const& other)
            : super(other)
              //m_lru(other.m_lru)
            {
                assert(n_args==other.n_args);
                for (uint_t i=0; i<n_args; ++i)
                    m_fields[i]=pointer_type(other.m_fields[i]);
                super::m_data=m_fields[0];
            }

            GT_FUNCTION_WARNING
        void copy_data_to_gpu(){
            //the fields are otherwise not copied to the gpu, since they are not inserted in the storage_pointers fusion vector
            for (uint_t i=0; i<n_args; ++i)
                m_fields[i].update_gpu();
        }

        // extend_width() : m_lru(0), m_fields(){
        // };

        GT_FUNCTION
        typename pointer_type::pointee_t* get_address() const {
            return super::get_address();}


        /**note that I pass in the lru integer in order to be able to define it as a constexpr static method*/
        GT_FUNCTION
        static constexpr ushort_t get_index_address (short_t const& offset/*, ushort_t const& lru*/) {
            return (/*lru+*/offset+n_args)%n_args;
        }

        //template <ushort_t Offset>
        GT_FUNCTION
        typename pointer_type::pointee_t* get_address(short_t offset) const {
            //printf("the offset: %d\n", offset);
            // printf("storage used: %d\n", (m_lru+offset+n_args)%n_args);
            // printf("GPU address of the data", m_fields[(m_lru+offset+n_args)%n_args].get());
            return m_fields[(/*m_lru+*/offset+n_args)%n_args].get();}
        //super::pointer_type const& data(){return m_fields[lru];}
        GT_FUNCTION
        pointer_type const& get_field(int index) const {return m_fields[index];};
        //the time integration takes ownership over all the pointers?
        GT_FUNCTION
        void swap(/*smart<*/ pointer_type/*>*/ & field){
            //cycle in a ring
            pointer_type swap(m_fields[n_args-1]);
            m_fields[n_args-1]=field;
            field = swap;
            //m_lru=(m_lru+1)%n_args;
            //this->m_data=m_fields[0];
        }

        /**@brief adds a given data field at the end of the buffer
           \param field the pointer to the input data field
         */
        GT_FUNCTION
        void push_back(/*smart<*/ const pointer_type/*>*/ & field, uint_t const& from=(uint_t)1, uint_t const& to=(uint_t)(n_args-1)){
            //cycle in a ring: better to shift all the pointers, so that we don't need to keep another indirection when accessing the storage (stateless storage)
            if(m_fields[to])
                m_fields[to].free_it();
            for(uint_t i=from;i<to+1;i++) m_fields[i]=m_fields[i-1];
            m_fields[0]=field;
            //m_lru=(m_lru+1)%n_args;
            //this->m_data=m_fields[m_lru];
        }


        /**@brief adds a new data field at the end of the buffer*/
        GT_FUNCTION
        void push_back_new(){
            //cycle in a ring: better to shift all the pointers, so that we don't need to keep another indirection when accessing the storage (stateless storage)
            push_back(new typename pointer_type::pointee_t(this->size()));
        }

        //the time integration takes ownership over all the pointers?
        GT_FUNCTION
        void advance(short_t offset=1){
            pointer_type tmp(m_fields[n_args-1]);
            for(uint_t i=1;i<n_args;i++) m_fields[i]=m_fields[i-1];
            m_fields[0]=tmp;
            //cycle in a ring
            //m_lru=(m_lru+n_args+offset)%n_args;
            //this->m_data=m_fields[m_lru];
        }

        GT_FUNCTION
        pointer_type const*  fields(){return m_fields;}

        void print() {
            print(std::cout);
        }

        template <typename Stream>
        void print(Stream & stream) {
            for(ushort_t i=0; i < n_args; ++i)
            {
                super::print(stream);
                advance();
            }
        }

        static const ushort_t n_args = ExtraWidth+1;

        //for stdcout purposes
        explicit extend_width(){}

    private:
        pointer_type m_fields[n_args];
    };

    /**specialization: if the width extension is 0 we fall back on the base storage*/
    template < typename Storage>
    struct extend_width<Storage, 0> : public Storage//, clonable_to_gpu<extend_width<Storage, 0> > //  : public Storage
    {
        typedef typename Storage::basic_type basic_type;
        typedef typename Storage::original_storage original_storage;

#ifdef CXX11_ENABLED
        //inheriting constructors
        using Storage::Storage;
#endif
        __device__
        extend_width(extend_width const& other)
            : Storage(other)
            {}
        static const ushort_t n_args = Storage::n_args;

        //for stdcout purposes
        explicit extend_width(){}

    };

#ifdef CXX11_ENABLED

    /** @brief first interface: each extend_width in the vector is specified with its own extra width
    	extension<extend_width<storage, 3>, extend_width<storage, 2>, extend_width<storage, 4> >, which is syntactic sugar for:
    	extension<extend_width<extension<extend_width<extension<extend_width<storage, 4> >, 2> >, storage, 3> >
    */
    template < typename First, typename  ...  StorageExtended>
    struct dimension_extension_traits : public dimension_extension_traits<StorageExtended ... > {
        //First must be an
        //total buffer size
        static const uint_t n_fields=First::n_args + dimension_extension_traits<StorageExtended  ...  >::n_fields ;
        //the buffer size of the current field (i.e. the total number of snapshots)
        static const uint_t n_width=First::n_args;
        //the number of dimensions (i.e. the number of different fields)
        static const uint_t n_dimensions=  dimension_extension_traits<StorageExtended  ...  >::n_dimensions  +1 ;
        //the current field extension
        // typedef extend_width<First, n_fields>  type;
        typedef extend_width<First, n_fields>  type;
        // typedef First type;
        typedef typename dimension_extension_traits<StorageExtended ... >::super super;
        // typedef typename super::pointer_type pointer_type;
        // typedef typename super::basic_type basic_type;
        // typedef typename super::original_storage original_storage;
         //inheriting constructors
        // using super::dimension_extension_traits;
   };

/**@brief template specialization at the end of the recustion.*/
    template < typename First>
    struct dimension_extension_traits<First>  {
        //total number of dimensions
        static const uint_t n_fields=First::n_args;
        static const uint_t n_width=First::n_args;
        static const uint_t n_dimensions= 1 ;
        // typedef extend_width<First, n_fields>  type;
        typedef First type;
        typedef First super;
        // typedef typename super::pointer_type pointer_type;
        // typedef typename super::basic_type basic_type;
        // typedef typename super::original_storage original_storage;
       //inheriting constructors
        // using super::First;
     };

    /**@brief metafunction to access a typelist at a given position*/
    template<uint_t ID, typename Sequence>
    struct access{
        typedef typename access<ID-1, typename Sequence::super>::type type;
    };

    template<typename Sequence>
    struct access<0, Sequence>{
        typedef typename Sequence::super type;
    };


    template <typename First,  typename  ...  StorageExtended>
    struct extend_dim : public dimension_extension_traits<First, StorageExtended ...  >::type, clonable_to_gpu<extend_dim<First, StorageExtended  ... > >
    {
        typedef typename dimension_extension_traits<First, StorageExtended ... >::type super;
        typedef dimension_extension_traits<First, StorageExtended ...  > traits;
        typedef typename super::pointer_type pointer_type;
        typedef typename  super::basic_type basic_type;
        typedef typename super::original_storage original_storage;
        //inheriting constructors
        using super::extend_width;
        using super::push_back;
        using super::push_back_new;

        __device__
        extend_dim( extend_dim const& other )
            : super(other)
            {}

        /**@brief pushes a given data field at the front of the buffer
           \param field the pointer to the input data field
         */
        template<uint_t dimension=0>
        GT_FUNCTION
        void push_back( const pointer_type & field ){
            //cycle in a ring: better to shift all the pointers, so that we don't need to keep another indirection when accessing the storage (stateless storage)

            BOOST_STATIC_ASSERT(dimension<super::n_dimensions);
            uint_t const indexFrom=access<dimension, traits>::type::n_args-1;
            uint_t const indexTo=access<dimension-1, traits>::type::n_args-1;

            this->push_back(field, indexFrom, indexTo);
            // if(m_fields[indexTo])
            //     m_fields[indexTo].free_it();
            // for(uint_t i=indexFrom ;i<=indexTo ;i++) m_fields[i]=m_fields[i-1];
            // m_fields[0]=field;
        }

        /**@brief adds a new data field at the front of the buffer*/
        template<uint_t dimension=0>
        GT_FUNCTION
        void push_back_new(){
            //cycle in a ring: better to shift all the pointers, so that we don't need to keep another indirection when accessing the storage (stateless storage)
            push_back(new typename pointer_type::pointee_t(this->size()), dimension);
        }

        //for stdcout purposes
        explicit extend_dim(){}
    };
#endif //CXX11_ENABLED

    template < enumtype::backend B, typename ValueType, typename Layout, bool IsTemporary
               >
    const std::string base_storage<B , ValueType, Layout, IsTemporary
                                   >::info_string=boost::lexical_cast<std::string>("-1");

    template <enumtype::backend B, typename ValueType, typename Y>
    struct is_temporary_storage<base_storage<B,ValueType,Y,false>*& >
        : boost::false_type
    {};

    template <enumtype::backend X, typename ValueType, typename Y>
    struct is_temporary_storage<base_storage<X,ValueType,Y,true>*& >
        : boost::true_type
    {};

    template <enumtype::backend X, typename ValueType, typename Y>
    struct is_temporary_storage<base_storage<X,ValueType,Y,false>* >
        : boost::false_type
    {};

    template <enumtype::backend X, typename ValueType, typename Y>
    struct is_temporary_storage<base_storage<X,ValueType,Y,true>* >
        : boost::true_type
    {};

    template <enumtype::backend X, typename ValueType, typename Y>
    struct is_temporary_storage<base_storage<X,ValueType,Y,false> >
        : boost::false_type
    {};

    template <enumtype::backend X, typename ValueType, typename Y>
    struct is_temporary_storage<base_storage<X,ValueType,Y,true> >
        : boost::true_type
    {};

    template <  template <typename T> class  Decorator, typename BaseType>
    struct is_temporary_storage<Decorator< BaseType > > : is_temporary_storage< typename BaseType::basic_type >
    {};
    template <  template <typename T> class Decorator, typename BaseType>
    struct is_temporary_storage<Decorator< BaseType >* > : is_temporary_storage< typename BaseType::basic_type* >
    {};
    template <  template <typename T> class Decorator, typename BaseType>
    struct is_temporary_storage<Decorator< BaseType >& > : is_temporary_storage< typename BaseType::basic_type& >
    {};
    template <  template <typename T> class Decorator, typename BaseType>
    struct is_temporary_storage<Decorator< BaseType >*& > : is_temporary_storage< typename BaseType::basic_type*& >
    {};

#ifdef CXX11_ENABLED
    //Decorator is the extend
    template <template <typename ... T> class Decorator, typename First, typename ... BaseType>
    struct is_temporary_storage<Decorator<First, BaseType...> > : is_temporary_storage< typename First::basic_type >
    {};

    //Decorator is the extend
    template <template <typename ... T> class Decorator, typename First, typename ... BaseType>
    struct is_temporary_storage<Decorator<First, BaseType...>* > : is_temporary_storage< typename First::basic_type* >
    {};

    //Decorator is the extend
    template <template <typename ... T> class Decorator, typename First, typename ... BaseType>
    struct is_temporary_storage<Decorator<First, BaseType...>& > : is_temporary_storage< typename First::basic_type& >
    {};

    //Decorator is the extend
    template <template <typename ... T> class Decorator, typename First, typename ... BaseType>
    struct is_temporary_storage<Decorator<First, BaseType...>*& > : is_temporary_storage< typename First::basic_type*& >
    {};
#endif //CXX11_ENABLED

    template <enumtype::backend Backend, typename T, typename U, bool B>
    std::ostream& operator<<(std::ostream &s, base_storage<Backend,T,U, B> ) {
        return s << "base_storage <T,U," << " " << std::boolalpha << B << "> ";
    }

#ifdef CXX11_ENABLED
    template <typename ... T>
    std::ostream& operator<<(std::ostream &s, extend_dim< T... > ) {
        return s << "extend_dim storage" ;
    }
#endif

} //namespace gridtools
