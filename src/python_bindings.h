// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <nanobind/nanobind.h>

namespace nb = nanobind;

void bind_camera(nb::module_ &m);
void bind_texture(nb::module_ &m);
void bind_shader_program(nb::module_ &m);
void bind_material(nb::module_ &m);
void bind_mesh_builder(nb::module_ &m);
void bind_draw_call(nb::module_ &m);
void bind_animation(nb::module_ &m);
void bind_model(nb::module_ &m);
void bind_renderer(nb::module_ &m);
void bind_log(nb::module_ &m);
void bind_lod(nb::module_ &m);

#include <functional>
#include <type_traits>
#include <utility>

namespace pyedge
{

/// Raises DeprecationWarning with @p message (an error where warnings are errors).
inline void warn_deprecated(char const *message)
{
	if (PyErr_WarnEx(PyExc_DeprecationWarning, message, 1) < 0)
		throw nb::python_error();
}

namespace detail
{

template <class F> struct operator_of;
template <class R, class L, class... A> struct operator_of<R (L::*)(A...) const> { using type = R(A...); };

template <class F> struct call_of: operator_of<decltype(&F::operator())> {};      // a lambda: what its operator() takes
template <class R, class C, class... A> struct call_of<R (C::*)(A...)> { using type = R(C &, A...); };
template <class R, class C, class... A> struct call_of<R (C::*)(A...) const> { using type = R(C const &, A...); };
template <class R, class... A> struct call_of<R (*)(A...)> { using type = R(A...); };

/// call_of, but a member function of a base of Self taking a Self: the class bound (whose base nanobind may not know)
template <class Self, class F> struct call_of_self: call_of<F> {};
template <class Self, class R, class C, class... A> struct call_of_self<Self, R (C::*)(A...)> { using type = R(Self &, A...); };
template <class Self, class R, class C, class... A> struct call_of_self<Self, R (C::*)(A...) const> { using type = R(Self const &, A...); };

template <class Signature> struct deprecated_call;
template <class R, class... A> struct deprecated_call<R(A...)>
{
	template <class F> static auto make(char const *message, F f)
	{
		return [message, f](A... a) -> R {
			warn_deprecated(message);
			return std::invoke(f, std::forward<A>(a)...);
		};
	}
};

}

/// @p f (a member function, a function or a lambda) under a name pyedge had before 2026-11: it warns (DeprecationWarning, @p message
/// saying what the name is now) and does what @p f does.  deprecated<Class>() for a member function of a base of the class bound.
template <class Self = void, class F> auto deprecated(char const *message, F f)
{
	if constexpr (std::is_void_v<Self>)
		return detail::deprecated_call<typename detail::call_of<F>::type>::make(message, f);
	else
		return detail::deprecated_call<typename detail::call_of_self<Self, F>::type>::make(message, f);
}

}
