#ifndef A7R3_ENTRY_CERT_H
#define A7R3_ENTRY_CERT_H

/*
 * Unhide and hook Setup Page 7's CertificationLogo (procId = 0x10f, "认证标识")
 * on Sony ILCE-7RM3 v3.10 in pure volatile RAM by cloning and swapping its
 * CmnViewProcessDataElement singleton vtable pointer. Zero .text patching.
 */
int a7r3_entry_cert_install(void);

/* Returns 1 once if the user executed CertificationLogo ("认证标识"). */
int a7r3_entry_cert_take_triggered(void);

/* Returns 1 if the native menu label hook matched since last call. */
int a7r3_entry_cert_take_label_hits(unsigned *out_hits);

/* Atomically restores the original CertificationLogo vptr if installed. */
void a7r3_entry_cert_remove(void);

#endif /* A7R3_ENTRY_CERT_H */
