AC_DEFUN([AC_INSTALL_HEADER_CONFIG], [
AC_CONFIG_COMMANDS($1/config.h,
                   [test config.h -nt $1/config.h &&
                        sed "s/define  */define $2_/g; s/def  */def $2_/g" \
                            config.h > $1/config.h])])
