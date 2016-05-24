package stringset

type StringSet map[string]bool

func New() StringSet {
	return make(StringSet)
}
func (s StringSet) Add(str string) {
	s[str] = true
}

func (s StringSet) Contains(str string) bool {
	return s[str]
}

func (s StringSet) AsList() []string {
	var ret = make([]string, 0, len(s))
	for k, _ := range s {
		ret = append(ret, k)
	}
	return ret
}

func (s StringSet) AddAll(other StringSet) {
	for k, _ := range other {
		s[k] = true
	}
}

func (s StringSet) String() string {
	ret := "{"
	first := true
	for k, _ := range s {
		if first {
			first = false
			ret += k
		} else {
			ret += " " + k
		}

	}
	ret += "}"
	return ret
}
