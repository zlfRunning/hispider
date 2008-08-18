AC_DEFUN([AC_CHECK_EXTRA_OPTIONS],[
        AC_MSG_CHECKING(for debugging)
        AC_ARG_ENABLE(debug, [  --enable-debug          compile for debugging])
        if test -z "$enable_debug" ; then
                enable_debug="no"
        elif test $enable_debug = "yes" ; then
                CPPFLAGS="${CPPFLAGS} -g -D_DEBUG"
        fi
        AC_MSG_RESULT([$enable_debug])
	AC_MSG_CHECKING(for linking static library )
        AC_ARG_ENABLE(link_static, [  --enable-link-static          link for being static])
        if test -z "$enable_link_static" ; then
                enable_link_static="no"
        elif test $enable_link_static = "yes" ; then
                LDFLAGS="${LDFLAGS} -static"
        fi
        AC_MSG_RESULT([$enable_link_static])
])

