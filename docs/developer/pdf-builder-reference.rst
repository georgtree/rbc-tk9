PDF builder C reference
=======================

These private services are declared in ``rbcExport.h`` and implemented in
``rbcPdf.c``. See :doc:`pdf-builder` for the single-use lifecycle and resource
numbering conventions. They are independent of Cairo.

.. doxygenfunction:: Rbc_PdfBegin

.. doxygenfunction:: Rbc_PdfScale

.. doxygenfunction:: Rbc_PdfHeight

.. doxygenfunction:: Rbc_PdfResource

.. doxygenfunction:: Rbc_PdfSetResource

.. doxygenfunction:: Rbc_PdfStream

.. doxygenfunction:: Rbc_PdfDocument

.. doxygenfunction:: Rbc_PdfFree
