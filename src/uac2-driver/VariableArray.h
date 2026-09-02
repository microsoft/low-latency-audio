// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    VariableArray.h

Abstract:

    Definition of a template for managing variable-length arrays.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _VARIABLEARRAY_H_
#define _VARIABLEARRAY_H_

#include <acx.h>

template <class T, ULONG I>
class VariableArray final
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    VariableArray() noexcept
    {
        PAGED_CODE();
        // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
        // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
    }

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    ~VariableArray() noexcept
    {
        // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
        if (m_memory != nullptr)
        {
            WdfObjectDelete(m_memory);
            m_memory = nullptr;
            m_array = nullptr;
        }
        m_capacity = 0;
        m_numOfArray = 0;
        // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
    }

    VariableArray(const VariableArray &) = delete;

    VariableArray & operator=(const VariableArray &) = delete;

    VariableArray(VariableArray &&) = delete;

    VariableArray & operator=(VariableArray &&) = delete;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Set(
        _In_ WDFOBJECT parentObject,
        _In_ ULONG     index,
        _In_ T         data
    )
    {
        NTSTATUS status = STATUS_SUCCESS;

        PAGED_CODE();

        if (index >= m_capacity)
        {
            WDFMEMORY memoryOld = m_memory;
            T *       arrayOld = m_array;
            ULONG     capacityOld = m_capacity;
            m_array = nullptr;
            if (index < I)
            {
                status = Allocate(parentObject, I);
            }
            else
            {
                status = Allocate(parentObject, index + I);
            }
            if (NT_SUCCESS(status))
            {
				if (arrayOld != nullptr)
                {
                    if (m_array != nullptr)
                    {
                        RtlCopyMemory(m_array, arrayOld, sizeof(T) * capacityOld);
                    }
                    if (memoryOld != nullptr)
                    {
                        WdfObjectDelete(memoryOld);
                        memoryOld = nullptr;
                        arrayOld = nullptr;
                    }
                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "delete arrayOld");
                }
                m_array[index] = data;
				m_numOfArray = (m_numOfArray > (index + 1)) ? m_numOfArray : (index + 1);
            }
        }
        else
        {
            m_array[index] = data;
			m_numOfArray = (m_numOfArray > (index + 1)) ? m_numOfArray : (index + 1);
        }
        return status;
    }

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS Get(
        _In_ ULONG index,
        _Out_ T &  data
    ) const noexcept
    {
        if (index >= m_numOfArray)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (m_array == nullptr)
        {
            return STATUS_UNSUCCESSFUL;
        }

        data = m_array[index];

        return STATUS_SUCCESS;
    }

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Append(
        _In_ WDFOBJECT parentObject,
        _In_ T         data
    )
    {
        PAGED_CODE();

        return Set(parentObject, m_numOfArray, data);
    }

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG GetNumOfArray() const noexcept
    {
        return m_numOfArray;
    }

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void Report() const
    {
        PAGED_CODE();

        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_capacity    = %d", m_capacity);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_numOfArray  = %d", m_numOfArray);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_array       = %p", m_array);

        if (m_array != nullptr)
        {
            for (ULONG index = 0; index < m_capacity; index++)
            {
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_array[%d] = %u", index, (ULONG)m_array[index]);
            }
        }
    }

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    T * begin() noexcept
    {
        return &(m_array[0]);
    }

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    T * end() noexcept
    {
        return &(m_array[m_numOfArray]);
    }

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const T * begin() const noexcept
    {
        return &(m_array[0]);
    }

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const T * end() const noexcept
    {
        return &(m_array[m_numOfArray]);
    }

  protected:
    WDFMEMORY m_memory{nullptr};
    T *       m_array{nullptr};
    ULONG     m_capacity{0};
    ULONG     m_numOfArray{0};

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Allocate(
        _In_ WDFOBJECT parentObject,
        _In_ ULONG     capacity
    )
    {
        WDF_OBJECT_ATTRIBUTES attributes;

        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = parentObject;

        NTSTATUS status = WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(T) * capacity, &m_memory, (PVOID *)&m_array);
        if (NT_SUCCESS(status))
        {
            RtlZeroMemory(m_array, sizeof(T) * capacity);

            m_capacity = capacity;
        }
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "Allocate(%d) ", capacity);

        return status;
    }
};

#endif
