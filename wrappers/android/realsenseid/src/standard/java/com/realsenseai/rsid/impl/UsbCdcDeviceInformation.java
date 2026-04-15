package com.realsenseai.rsid.impl;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.Objects;

/**
 * Represents information about a USB CDC device.
 * This class is immutable.
 */
public final class UsbCdcDeviceInformation {
    private final int deviceVendorId;
    private final int productId;
    @Nullable
    private final String productName;

    /**
     * Creates a new USB CDC device information instance.
     *
     * @param deviceVendorId The vendor ID of the device
     * @param productId The product ID of the device
     * @param productName The optional name of the product
     * @throws IllegalArgumentException if deviceVendorId or productId is negative
     */
    public UsbCdcDeviceInformation(int deviceVendorId, int productId, @Nullable String productName) {
        this.deviceVendorId = deviceVendorId;
        this.productId = productId;
        this.productName = productName;
    }

    /**
     * Gets the product name of the device.
     *
     * @return The product name, or null if not available
     */
    @Nullable
    public String getProductName() {
        return productName;
    }

    /**
     * Gets the product ID of the device.
     *
     * @return The product ID
     */
    public int getProductId() {
        return productId;
    }

    /**
     * Gets the vendor ID of the device.
     *
     * @return The vendor ID
     */
    public int getDeviceVendorId() {
        return deviceVendorId;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        UsbCdcDeviceInformation that = (UsbCdcDeviceInformation) o;
        return deviceVendorId == that.deviceVendorId &&
               productId == that.productId &&
               Objects.equals(productName, that.productName);
    }

    @Override
    public int hashCode() {
        return Objects.hash(deviceVendorId, productId, productName);
    }

    @NonNull
    @Override
    public String toString() {
        return "UsbCdcDeviceInformation{" +
               "deviceVendorId=0x" + Integer.toHexString(deviceVendorId) +
               ", productId=0x" + Integer.toHexString(productId) +
               ", productName='" + productName + '\'' +
               '}';
    }

    /**
     * Builder for creating UsbCdcDeviceInformation instances.
     */
    public static class Builder {
        private int deviceVendorId = -1;
        private int productId = -1;
        private String productName = null;

        /**
         * Sets the device vendor ID.
         *
         * @param deviceVendorId The vendor ID to set
         * @return This builder instance
         * @throws IllegalArgumentException if deviceVendorId is negative
         */
        public Builder setDeviceVendorId(int deviceVendorId) {
            if (deviceVendorId < 0) {
                throw new IllegalArgumentException("Device vendor ID cannot be negative");
            }
            this.deviceVendorId = deviceVendorId;
            return this;
        }

        /**
         * Sets the product ID.
         *
         * @param productId The product ID to set
         * @return This builder instance
         * @throws IllegalArgumentException if productId is negative
         */
        public Builder setProductId(int productId) {
            if (productId < 0) {
                throw new IllegalArgumentException("Product ID cannot be negative");
            }
            this.productId = productId;
            return this;
        }

        /**
         * Sets the product name.
         *
         * @param productName The product name to set
         * @return This builder instance
         */
        public Builder setProductName(@Nullable String productName) {
            this.productName = productName;
            return this;
        }

        /**
         * Builds a new UsbCdcDeviceInformation instance.
         *
         * @return A new UsbCdcDeviceInformation instance
         * @throws IllegalStateException if required fields are not set
         */
        public UsbCdcDeviceInformation build() {
            if (deviceVendorId < 0) {
                throw new IllegalStateException("Device vendor ID must be set");
            }
            if (productId < 0) {
                throw new IllegalStateException("Product ID must be set");
            }
            return new UsbCdcDeviceInformation(deviceVendorId, productId, productName);
        }
    }
}