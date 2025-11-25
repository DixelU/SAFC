#include <atomic>
#include <type_traits>
#include <utility>
#include <vector>
#include <memory>

namespace soa
{
	template<typename>
	class function_ref;

	namespace detail
	{
		template<typename T>
		static T& forward(T& t) { return t; }
		template<typename T>
		static T&& forward(T&& t) { return static_cast<T&&>(t); }

		template<typename C, typename R, typename... Args>
		constexpr auto get_call(R(C::* o)(Args...)) // We take the argument for sfinae
			-> typename function_ref<R(Args...)>::ptr_t
		{
			return [](void* t, Args... args) { return (static_cast<C*>(t)->operator())(forward<Args>(args)...); };
		}

		template<typename C, typename R, typename... Args>
		constexpr auto get_call(R(C::* o)(Args...) const) // We take the argument for sfinae
			-> typename function_ref<R(Args...)>::ptr_t
		{
			return [](void* t, Args... args) { return (static_cast<const C*>(t)->operator())(forward<Args>(args)...); };
		}

		template<typename R, typename... Args>
		constexpr auto expand_call(R(*)(Args...))
			-> typename function_ref<R(Args...)>::ptr_t
		{
			return [](void* t, Args... args) { return (reinterpret_cast<R(*)(Args...)>(t))(forward<Args>(args)...); };
		}
	}

	template<typename R, typename... Args>
	class function_ref<R(Args...)>
	{
	public:
		using signature_t = R(Args...);
		using ptr_t = R(*)(void*, Args...);
	private:
		void* self;
		ptr_t function;
	public:

		function_ref() = default;
		function_ref(const function_ref<signature_t>& ref) = default;
		function_ref(function_ref<signature_t>&& ref) = default;

		function_ref& operator=(function_ref&& ref) = default;
		function_ref& operator=(const function_ref& ref) = default;

		template<typename C>
		function_ref(C* c) : // Pointer to embrace that we do not manage this object
			self(c),
			function(detail::get_call(&C::operator()))
		{ }

		using rawfn_ptr_t = R(*)(Args...);
		function_ref(rawfn_ptr_t fnptr) :
			self(reinterpret_cast<void*>(fnptr)),
			function(detail::expand_call(fnptr))
		{ }

		R operator()(Args... args) const
		{
			return function(self, detail::forward<Args>(args)...);
		}
	};

} // namespace soa

namespace dixelu
{
	template<typename>
	struct type_erased_function_container;

	template<typename R, typename... Args>
	struct type_erased_function_container<R(Args...)>
	{
		using arg_type = R(Args...);
		using self_type = type_erased_function_container<R(Args...)>;

	private:
		std::shared_ptr<void> _callable;
		soa::function_ref<R(Args...)> _function_ref;

		static R __dummy_func(Args...) { return R(); }
	public:

		type_erased_function_container() :
			_callable(nullptr),
			_function_ref(&__dummy_func)
		{}

		template<typename F>
		type_erased_function_container(F&& f) :
			_callable(
				new typename std::decay<F>::type(std::move(f)),
				[](void* ptr) {delete (static_cast<typename std::decay<F>::type*>(ptr)); }),
			_function_ref(static_cast<typename std::decay<F>::type*>(_callable.get()))
		{
		}

		template<typename F>
		type_erased_function_container(const F& f) :
			type_erased_function_container((typename std::decay<F>::type)(f))
		{
		}

		type_erased_function_container(self_type&& f) = default;
		type_erased_function_container(const self_type& f) = default;

		type_erased_function_container& operator=(type_erased_function_container&& ref) = default;
		type_erased_function_container& operator=(const type_erased_function_container& ref) = default;

		~type_erased_function_container()
		{
			_function_ref = decltype(_function_ref){&__dummy_func};
		}

		inline R operator()(Args... args) const { return (_function_ref)(soa::detail::forward<Args>(args)...); }
		operator bool() const { return _callable.get(); }
	};
}
