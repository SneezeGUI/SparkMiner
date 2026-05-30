(ns miner.binascii
  "Small collection of helper functions for converting between
   hexadecimal and byte representations of data")


(defn hex-to-byte
  "Converts a hex value into its byte value while
   taking care to wrap around unsigned
   values to their signed equivalent" ; due to Java being lame
  [hex-string]
  (-> hex-string
      (Integer/valueOf 16)
      (.byteValue)
      (byte)))


(defn unhexify
  "Converts the hexadecimal representation to a byte-array"
  [hex]
  (byte-array
    (map
      (fn [[x y]] (hex-to-byte (str x y)))
      (partition 2 hex))))


(defn to-hex [num width]
  (format (format "%%0%sx" width) num))


(defn hexify [s]
  (apply str
         (map #(format "%02x" (byte %)) s)))


(defn swap-endianness
  "Reverses the endianness of a hex string"
  [s]
  (reduce
    (fn [acc [x y]] (str acc y x))
    ""
    (partition 2 (reverse s))))
