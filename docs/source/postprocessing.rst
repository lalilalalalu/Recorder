Post-processing and Visualization
=================================

1. recorder-viz
---------------

We developed a Python library,
`recorder-viz <https://github.com/wangvsa/recorder-viz>`__, for
post-processing and visualizations. 


recorder-viz can be installed by using `pip`.
It relies on a few python libraries for visualization. Please install them first.
Dependencies: pandas, bokeh and prettytable.

.. code:: bash

   pip install numpy pandas prettytable
   pip install recorder-viz --user

This will install recorder-viz locally. You may need to include
``~/.local/bin`` in your ``$PATH`` enviroment.

.. code:: bash

   export PATH=$HOME/.local/bin:$PATH

recorder-viz relies on Recorder to run. Make sure ``$RECORDER_INSTALL_PATH``
points to the location where you installed Recorder.

To generate a visualization report. Run:

.. code:: bash

   recorder-report /path/to/your_trace_folder/

This will generate an html file under the current directory. Simply open
it with your web broswer. Cheers!


2. Format Converters
--------------------

We also provide two format converters ``recorder2parquet`` and
``recorder2timeline``. They will be placed under $RECORDER_ROOT/bin
directory after installation.

-  ``recorder2parquet`` will convert Recorder traces into a single
   `Parquet <https://parquet.apache.org>`__ formata file. The Apache
   Parquet format is a well-known format that is supported by many
   analysis tools.

-  ``recorder2timeline`` will conver Recorder traces into
   `Chromium <https://www.chromium.org/developers/how-tos/trace-event-profiling-tool/trace-event-reading>`__
   trace format files. You can upload them to https://ui.perfetto.dev
   for an interactive visualization.

3. APIs
---------

TODO: we have C APIs (tools/reader.h). Need to doc them.

