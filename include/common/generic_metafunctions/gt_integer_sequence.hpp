#pragma once

#include <functional>

namespace gridtools{

#ifdef CXX11_ENABLED

#ifndef CXX14_ENABLED
    /**
       @brief helper struct to use an integer sequence in order to fill a generic container

       can be used with an arbitrary container with elements of the same type (not a tuple),
       it is consexpr constructable.
     */
    template< typename UInt, UInt... Indices> struct integer_sequence{
        using type = integer_sequence;
    };


    /** @bief concatenates two integer sequences*/
    template<class S1, class S2> struct concat;

    template<typename UInt, UInt... I1, UInt... I2>
    struct concat<integer_sequence<UInt, I1...>, integer_sequence<UInt, I2...>>
        : integer_sequence<UInt, I1..., (sizeof...(I1)+I2)...>{};

    /** @brief constructs an integer sequence

        @tparam N number larger than 2, size of the integer sequence
     */
    template<typename UInt, uint_t N>
    struct make_integer_sequence : concat<typename make_integer_sequence<UInt, N/2>::type, typename make_integer_sequence<UInt, N - N/2>::type >::type{};

    template<typename UInt> struct make_integer_sequence<UInt, 0> : integer_sequence<UInt>{};
    template<typename UInt> struct make_integer_sequence<UInt, 1> : integer_sequence<UInt,0>{};
#else
    template <typename UInt, UInt ... Indices>
    using integer_sequence=std::integer_sequence<UInt, Indices ...>;

    template<typename UInt, uint_t N>
    using make_integer_sequence=std::make_integer_sequence<UInt, N>;
#endif

    /** @brief constructs and returns a Container initialized by Lambda<I>::apply(args_...)
        for all the indices I in the sequence

        @tparam Container is the container to be filled
        @tparam Lambda is a metafunction templated with an integer, whose static member
        function "apply" returns an element of the container
        @tparam ExtraTypes are the types of the arguments to the method "apply" (deduced by the compiler)

        The type of the Container members must correspond to the return types of the apply method in
        the user-defined Lambda functor.
    */

    template< typename T>
    struct apply_integer_sequence;

    template< typename UInt, UInt... Indices>
    struct apply_integer_sequence<integer_sequence<UInt, Indices ...> >
    {

    template<typename Container, template <UInt T> class Lambda, typename ... ExtraTypes>
        GT_FUNCTION
        static constexpr Container apply(ExtraTypes const& ... args_ ){
            return Container(Lambda<Indices>::apply(args_...) ...) ;
        }

        /**
           @brief same as before, but with non-static lambda taking as first argument the index
         */
        template<typename Container, class Lambda, typename ... ExtraTypes>
        GT_FUNCTION
        static constexpr Container apply(Lambda lambda, ExtraTypes& ... args_ ){
            return Container(lambda(Indices, args_...) ...) ;
        }

    };

#endif
} //namespace gridtools
