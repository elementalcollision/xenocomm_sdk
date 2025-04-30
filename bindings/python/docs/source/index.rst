.. xenocomm documentation master file, created by
   sphinx-quickstart on Wed Apr 30 14:13:19 2025.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

xenocomm documentation
======================

Quickstart Example
------------------

.. code-block:: python

    from xenocomm import ConnectionManager

    # Create a connection manager and connect to a server
    with ConnectionManager("localhost", 8080) as conn:
        if conn.is_connected():
            print("Connected!")
        # ... perform operations ...
    # Connection is automatically closed on exit

API Reference
-------------

.. automodule:: xenocomm
    :members:
    :undoc-members:
    :show-inheritance:

.. toctree::
   :maxdepth: 2
   :caption: Contents:

