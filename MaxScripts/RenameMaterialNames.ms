--for m = 1 to sceneMaterials.count do sceneMaterials[m].name = "mat" + m as string
--for m = 1 to sceneMaterials.count do print sceneMaterials[m].name
global index = 0

function ChangeMtl mtl =
(
	print mtl.name
	mtl.name = "mtl_" + index as string
	index = index + 1
	
	local numsub = getNumSubMtls mtl
	if numsub > 0 then
	(
		for n = 1 to numsub do
		(
			ChangeMtl (getSubMtl mtl n)
		)
	)
	else
	(
		if(mtl.shaderType != 1) then
		(
			mtl.shaderType = 1
		)
		
		if mtl.diffuse == (Color 0 0 0) then
		(
			--print "black color"
			mtl.diffuse = (Color 0 0 0)
		)
		
		if mtl.bumpMap != undefined then
		(
			if mtl.diffuseMap == undefined then
			(
				mtl.diffuseMap = mtl.bumpMap
			)
		)
		--print mtl.diffuse
	)
)


for m = 1 to sceneMaterials.count do 
(
	ChangeMtl sceneMaterials[m]
)

