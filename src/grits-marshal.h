
#ifndef __grits_cclosure_marshal_MARSHAL_H__
#define __grits_cclosure_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:DOUBLE,DOUBLE,DOUBLE (grits-marshal.list:1) */
extern void grits_cclosure_marshal_VOID__DOUBLE_DOUBLE_DOUBLE (GClosure     *closure,
                                                               GValue       *return_value,
                                                               guint         n_param_values,
                                                               const GValue *param_values,
                                                               gpointer      invocation_hint,
                                                               gpointer      marshal_data);

/* VOID:STRING,UINT,POINTER (grits-marshal.list:2) */
extern void grits_cclosure_marshal_VOID__STRING_UINT_POINTER (GClosure     *closure,
                                                              GValue       *return_value,
                                                              guint         n_param_values,
                                                              const GValue *param_values,
                                                              gpointer      invocation_hint,
                                                              gpointer      marshal_data);

/* BOOLEAN:POINTER (grits-marshal.list:3) */
extern void grits_cclosure_marshal_BOOLEAN__POINTER (GClosure     *closure,
                                                     GValue       *return_value,
                                                     guint         n_param_values,
                                                     const GValue *param_values,
                                                     gpointer      invocation_hint,
                                                     gpointer      marshal_data);

G_END_DECLS

#endif /* __grits_cclosure_marshal_MARSHAL_H__ */

