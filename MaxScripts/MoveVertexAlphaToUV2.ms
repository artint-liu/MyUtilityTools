tar_uv = 3

for k = 1 to selection.count do
(	
	format "name[%]:%\n" k selection[k].name
	obj = convertToMesh selection[k]
	if(obj==undefined ) then continue
	obj.vertexColorType = #alpha

	meshop.setNumMaps obj (tar_uv + 1) keep:true

	vert_alpha = meshop.getMapSupport obj -2
	format "num map %\n" (meshop.getNumMaps obj)
	format "vertex alpha support: %\n" vert_alpha
	if vert_alpha==false then
	(
		for i = 1 to getNumVerts obj do
		(
			meshop.setMapVert obj tar_uv i [1, 1, 0]
		)
		continue 
	)
		
	map_verts = meshop.getNumMapVerts obj -2
	map_faces = meshop.getNumMapFaces obj -2
	--format "vert:% vert alpha:%\n" num_verts map_verts
	meshop.setNumMapVerts obj tar_uv map_verts keep:false
	meshop.setNumMapFaces obj tar_uv map_faces keep:false keepCount:2
	--meshop.defaultMapFaces obj 2
	for n = 1 to map_faces do
	(
		alpha_face = meshop.getMapFace obj -2 n
		meshop.setMapFace obj tar_uv n alpha_face
	)

	for i = 1 to map_verts do
	(
		vert = meshop.getMapVert obj -2 i
		newVert = [vert.x,vert.x,0]
		meshop.setMapVert obj tar_uv i newVert
	)
	update obj
)
